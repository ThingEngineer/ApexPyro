#include "continuity.h"
#include "storage.h"
#include "relay_manager.h"
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// Global ADC instance
Adafruit_ADS1115 ads;

ContinuityManager continuityManager;

namespace {
static const uint32_t ADS1115_RECOVERY_RETRY_MS = 1000;
}

ContinuityManager::ContinuityManager()
    : adsAvailable(false), lastAdsRecoveryAttemptMs(0), lastScanMs(0), lastBatteryScanMs(0), currentMuxPosition(0),
      threshLoGood(DEFAULT_CONTINUITY_LOW_GOOD),
      threshHiGood(DEFAULT_CONTINUITY_HI_GOOD),
      threshLoOpen(DEFAULT_CONTINUITY_LOW_OPEN_CIRCUIT) {
    
    // Initialize all zones to unknown
    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        zoneStatus[i] = ContinuityStatus::UNKNOWN;
    }
    
}

void ContinuityManager::begin() {
    // Storage is initialized by main before this point.
    threshLoGood = storage.getContinuityLoGood();
    threshHiGood = storage.getContinuityHiGood();
    threshLoOpen = storage.getContinuityLoOpen();

    lastAdsRecoveryAttemptMs = millis();
    if (!initializeAds(false)) {
        return;
    }

    Serial.printf("[ContinuityManager] Continuity thresholds: lo_good=%.4fV, hi_good=%.4fV, lo_open=%.4fV\n",
                  threshLoGood, threshHiGood, threshLoOpen);
}

void ContinuityManager::update() {
    if (!adsAvailable) {
        uint32_t now = millis();
        if (now - lastAdsRecoveryAttemptMs >= ADS1115_RECOVERY_RETRY_MS) {
            lastAdsRecoveryAttemptMs = now;
            initializeAds(true);
        }
        return;
    }

    if (!probeAdsConnection()) {
        handleAdsUnavailable("communication lost");
        return;
    }

    uint32_t now = millis();
    
    // Scan zones periodically
    if (now - lastScanMs >= CONTINUITY_SCAN_INTERVAL_MS) {
        scanAllZones();
        lastScanMs = now;
    }
    
    // Read battery periodically
    if (now - lastBatteryScanMs >= 5000) {  // Every 5 seconds
        readBatteryVoltage();
        lastBatteryScanMs = now;
    }
}

bool ContinuityManager::initializeAds(bool isRecoveryAttempt) {
    if (!ads.begin(ADS1115_I2C_ADDR)) {
        adsAvailable = false;
        if (isRecoveryAttempt) {
            return false;
        }

        Serial.printf("[ContinuityManager] ADS1115 not found at 0x%02X; continuity scan and battery reads disabled\n",
                      ADS1115_I2C_ADDR);
        return false;
    }

    ads.setGain(GAIN_ONE);

    if (isRecoveryAttempt) {
        Serial.printf("[ContinuityManager] ADS1115 communication restored at 0x%02X\n", ADS1115_I2C_ADDR);
    } else {
        Serial.printf("[ContinuityManager] ADS1115 initialized at 0x%02X\n", ADS1115_I2C_ADDR);
    }

    adsAvailable = true;
    return true;
}

bool ContinuityManager::probeAdsConnection() {
    Wire.beginTransmission(ADS1115_I2C_ADDR);
    return Wire.endTransmission() == 0;
}

void ContinuityManager::handleAdsUnavailable(const char* reason) {
    if (!adsAvailable) {
        return;
    }

    adsAvailable = false;
    lastAdsRecoveryAttemptMs = millis();
    Serial.printf("[ContinuityManager] ADS1115 %s at 0x%02X; continuity scan paused\n",
                  reason, ADS1115_I2C_ADDR);
}

void ContinuityManager::setMuxPosition(uint8_t position) {
    // CD74HC4067 uses S0-S3 to select one of 16 channels
    // Each of the 3 MUX ICs is independently selectable via same S0-S3 lines
    currentMuxPosition = position & 0x0F;  // 0-15
    
    // Set select lines
    digitalWrite(MUX_S0_PIN, (currentMuxPosition & 0x01) ? HIGH : LOW);
    digitalWrite(MUX_S1_PIN, (currentMuxPosition & 0x02) ? HIGH : LOW);
    digitalWrite(MUX_S2_PIN, (currentMuxPosition & 0x04) ? HIGH : LOW);
    digitalWrite(MUX_S3_PIN, (currentMuxPosition & 0x08) ? HIGH : LOW);
    
    delayMicroseconds(MUX_SETTLING_TIME_MS * 1000);
}

float ContinuityManager::readAdcChannel(uint8_t channel) {
    if (!adsAvailable) {
        return 0.0f;
    }

    if (channel > 3) return 0.0f;
    
    int16_t adc = ads.readADC_SingleEnded(channel);
    // Convert ADC value to voltage
    // With GAIN_ONE (±4.096V), each count is 0.125mV
    float voltage = adc * 0.125f / 1000.0f;  // Convert mV to V
    return voltage;
}

void ContinuityManager::scanAllZones() {
    // Scan all 16 MUX positions
    // Each position gives us 3 zone readings (from 3 MUXes on A0, A1, A2)
    for (uint8_t mux_pos = 0; mux_pos < 16; mux_pos++) {
        setMuxPosition(mux_pos);
        
        // Read from each MUX output on different ADC channels
        float volt_mux0 = readAdcChannel(ADS1115_CHANNEL_CONTINUITY_MUX0);
        float volt_mux1 = readAdcChannel(ADS1115_CHANNEL_CONTINUITY_MUX1);
        float volt_mux2 = readAdcChannel(ADS1115_CHANNEL_CONTINUITY_MUX2);
        
        uint8_t zone_idx0 = mux_pos;                    // MUX0: zones 0-15
        uint8_t zone_idx1 = mux_pos + 16;               // MUX1: zones 16-31
        uint8_t zone_idx2 = mux_pos + 32;               // MUX2: zones 32-47
        
        // Classify and store
        zoneStatus[zone_idx0] = classifyVoltage(volt_mux0);
        if (zone_idx1 < MAX_ZONES) zoneStatus[zone_idx1] = classifyVoltage(volt_mux1);
        if (zone_idx2 < MAX_ZONES) zoneStatus[zone_idx2] = classifyVoltage(volt_mux2);
    }
}

ContinuityStatus ContinuityManager::classifyVoltage(float voltage) {
    // If zone is actively firing, Zener is clamping to ~3.3V
    if (voltage >= 3.0f && voltage <= 3.6f) {
        return ContinuityStatus::FIRING;
    }
    
    // Good e-match: low test current drops across 2Ω igniter
    if (voltage >= threshLoGood && voltage <= threshHiGood) {
        return ContinuityStatus::GOOD;
    }
    
    // Near 0V or very low = no e-match or open circuit
    if (voltage < threshLoGood) {
        return ContinuityStatus::UNKNOWN;  // Could be open or unconnected
    }
    
    // Above high threshold = open circuit (high Z)
    if (voltage >= threshLoOpen) {
        return ContinuityStatus::OPEN_CIRCUIT;
    }
    
    // Shorted or very low impedance (but not firing)
    return ContinuityStatus::SHORTED;
}

ContinuityStatus ContinuityManager::getZoneStatus(uint8_t zoneIdx) {
    if (zoneIdx < MAX_ZONES) {
        return zoneStatus[zoneIdx];
    }
    return ContinuityStatus::UNKNOWN;
}

void ContinuityManager::readBatteryVoltage() {
    // Read from ADS1115 channel 3 (battery voltage divider)
    float adcVolt = readAdcChannel(ADS1115_CHANNEL_BATTERY);
    float batVolt = adcVolt * VBAT_SCALE;
    
    // This is called by UI handlers; see getBatteryVoltage() and getBatteryPercent()
}

float ContinuityManager::getBatteryVoltage() {
    if (!adsAvailable) {
        return 0.0f;
    }

    float adcVolt = readAdcChannel(ADS1115_CHANNEL_BATTERY);
    return adcVolt * VBAT_SCALE;
}

int ContinuityManager::getBatteryPercent() {
    float batVolt = getBatteryVoltage();
    
    // LiFePO4 16S (51.2V nominal, ~40V min, ~58.4V max)
    // Map to 0-100% in 5% increments
    if (batVolt >= VBAT_MAX_PACK) return 100;
    if (batVolt <= VBAT_MIN_PACK) return 0;
    
    // Linear interpolation
    float percent = ((batVolt - VBAT_MIN_PACK) / (VBAT_MAX_PACK - VBAT_MIN_PACK)) * 100.0f;
    
    // Round to nearest 5%
    int percentInt = (int)((percent + 2.5f) / 5.0f) * 5;
    if (percentInt > 100) percentInt = 100;
    if (percentInt < 0) percentInt = 0;
    
    return percentInt;
}

float ContinuityManager::getLowGoodThreshold() {
    return threshLoGood;
}

float ContinuityManager::getHiGoodThreshold() {
    return threshHiGood;
}

float ContinuityManager::getLoOpenThreshold() {
    return threshLoOpen;
}

void ContinuityManager::setThresholds(float loGood, float hiGood, float loOpen) {
    threshLoGood = loGood;
    threshHiGood = hiGood;
    threshLoOpen = loOpen;
    
    storage.setContinuityThresholds(loGood, hiGood, loOpen);
    Serial.printf("[ContinuityManager] Thresholds updated: %.4f, %.4f, %.4f\n", loGood, hiGood, loOpen);
}
