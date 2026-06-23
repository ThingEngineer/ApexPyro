#include "continuity.h"
#include "storage.h"
#include "logger.h"
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
    threshLoOpen(DEFAULT_CONTINUITY_LOW_OPEN_CIRCUIT),
    batteryVoltageRaw(0.0f), batteryVoltageFiltered(0.0f), batteryPercent(0),
    lastBatteryConfigRefreshMs(0), lastBatteryPercentCalcMs(0) {
    
    // Initialize all zones to unknown
    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        zoneStatus[i] = ContinuityStatus::UNKNOWN;
    }

    batteryProfile.profile = BatteryProfile::LIFEPO4;
    batteryProfile.cellCount = DEFAULT_BATTERY_CUSTOM_CELL_COUNT;
    batteryProfile.packMin = DEFAULT_BATTERY_CUSTOM_PACK_MIN;
    batteryProfile.packMax = DEFAULT_BATTERY_CUSTOM_PACK_MAX;
    batteryProfile.lowWarn = DEFAULT_BATTERY_CUSTOM_LOW_WARN;
    batteryProfile.sampleIntervalMs = DEFAULT_BATTERY_SAMPLE_INTERVAL_MS;
    batteryProfile.pointCount = DEFAULT_BATTERY_CUSTOM_POINT_COUNT;
    for (uint8_t i = 0; i < BATTERY_MAX_CURVE_POINTS; i++) {
        batteryProfile.points[i] = DEFAULT_BATTERY_CUSTOM_POINTS[i];
    }
    
}

void ContinuityManager::begin() {
    // Storage is initialized by main before this point.
    threshLoGood = storage.getContinuityLoGood();
    threshHiGood = storage.getContinuityHiGood();
    threshLoOpen = storage.getContinuityLoOpen();
    refreshBatteryProfile(true);

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
    refreshBatteryProfile();
    
    // Scan zones periodically
    if (now - lastScanMs >= CONTINUITY_SCAN_INTERVAL_MS) {
        scanAllZones();
        lastScanMs = now;
    }
    
    // Read battery using configured sample interval.
    if (now - lastBatteryScanMs >= batteryProfile.sampleIntervalMs) {
        readBatteryVoltage();
        lastBatteryScanMs = now;
    }
}

void ContinuityManager::applyPresetProfile(BatteryProfile profileId, uint8_t overrideCellCount) {
    const BatteryProfilePreset* preset = getBatteryProfilePreset(profileId);
    batteryProfile.profile = profileId;
    batteryProfile.cellCount = overrideCellCount > 0 ? overrideCellCount : preset->defaultCellCount;
    batteryProfile.sampleIntervalMs = storage.getBatterySampleIntervalMs();
    batteryProfile.pointCount = preset->pointCount;

    for (uint8_t i = 0; i < preset->pointCount; i++) {
        batteryProfile.points[i].voltage = preset->points[i].voltage * batteryProfile.cellCount;
        batteryProfile.points[i].soc = preset->points[i].soc;
    }

    batteryProfile.packMin = preset->points[0].voltage * batteryProfile.cellCount;
    batteryProfile.packMax = preset->points[preset->pointCount - 1].voltage * batteryProfile.cellCount;
    batteryProfile.lowWarn = preset->lowWarnCell * batteryProfile.cellCount;
}

void ContinuityManager::refreshBatteryProfile(bool force) {
    uint32_t now = millis();
    if (!force && (now - lastBatteryConfigRefreshMs) < 1000) {
        return;
    }
    lastBatteryConfigRefreshMs = now;

    BatteryProfile profileId = static_cast<BatteryProfile>(storage.getBatteryProfileId());
    if (profileId != BatteryProfile::CUSTOM) {
        applyPresetProfile(profileId, 0);
        return;
    }

    batteryProfile.profile = BatteryProfile::CUSTOM;
    batteryProfile.cellCount = storage.getBatteryCellCount();
    batteryProfile.packMin = storage.getBatteryPackMin();
    batteryProfile.packMax = storage.getBatteryPackMax();
    batteryProfile.lowWarn = storage.getBatteryLowWarn();
    batteryProfile.sampleIntervalMs = storage.getBatterySampleIntervalMs();

    storage.getBatteryCurve(batteryProfile.points, batteryProfile.pointCount);
    if (batteryProfile.pointCount < 2) {
        batteryProfile.pointCount = DEFAULT_BATTERY_CUSTOM_POINT_COUNT;
        for (uint8_t i = 0; i < BATTERY_MAX_CURVE_POINTS; i++) {
            batteryProfile.points[i] = DEFAULT_BATTERY_CUSTOM_POINTS[i];
        }
    }

    if (batteryProfile.packMax <= batteryProfile.packMin) {
        batteryProfile.packMax = batteryProfile.packMin + 1.0f;
    }
    batteryProfile.lowWarn = constrain(batteryProfile.lowWarn, batteryProfile.packMin, batteryProfile.packMax);
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
    // Current continuity hardware path is fixed to 3 MUX channels (48 zones).
    // Zones above 47 remain UNKNOWN until continuity hardware is expanded.
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
    batteryVoltageRaw = adcVolt * VBAT_SCALE;

    if (batteryVoltageFiltered <= 0.0f) {
        batteryVoltageFiltered = batteryVoltageRaw;
    } else {
        const float alpha = 0.35f;
        batteryVoltageFiltered = (alpha * batteryVoltageRaw) + ((1.0f - alpha) * batteryVoltageFiltered);
    }

    batteryPercent = calculateBatteryPercent(batteryVoltageFiltered);
    lastBatteryPercentCalcMs = millis();
}

float ContinuityManager::getBatteryVoltage() {
    if (!adsAvailable) {
        return 0.0f;
    }

    if (batteryVoltageFiltered <= 0.0f) {
        readBatteryVoltage();
    }

    return batteryVoltageFiltered;
}

int ContinuityManager::getBatteryPercent() {
    if (!adsAvailable) {
        return 0;
    }

    refreshBatteryProfile();
    uint32_t now = millis();
    if (batteryVoltageFiltered <= 0.0f || now - lastBatteryPercentCalcMs > (batteryProfile.sampleIntervalMs + 1000)) {
        readBatteryVoltage();
    }

    return batteryPercent;
}

int ContinuityManager::calculateBatteryPercent(float packVoltage) const {
    if (batteryProfile.pointCount < 2) {
        if (packVoltage <= batteryProfile.packMin) return 0;
        if (packVoltage >= batteryProfile.packMax) return 100;
        float ratio = (packVoltage - batteryProfile.packMin) / (batteryProfile.packMax - batteryProfile.packMin);
        return static_cast<int>(constrain(static_cast<int>(ratio * 100.0f), 0, 100));
    }

    if (packVoltage <= batteryProfile.points[0].voltage) {
        return batteryProfile.points[0].soc;
    }

    if (packVoltage >= batteryProfile.points[batteryProfile.pointCount - 1].voltage) {
        return batteryProfile.points[batteryProfile.pointCount - 1].soc;
    }

    for (uint8_t i = 0; i + 1 < batteryProfile.pointCount; i++) {
        const BatteryCurvePoint& left = batteryProfile.points[i];
        const BatteryCurvePoint& right = batteryProfile.points[i + 1];
        if (packVoltage < left.voltage || packVoltage > right.voltage) {
            continue;
        }

        float span = right.voltage - left.voltage;
        if (span <= 0.0001f) {
            return right.soc;
        }

        float t = (packVoltage - left.voltage) / span;
        float interpolated = left.soc + ((right.soc - left.soc) * t);
        int rounded = static_cast<int>(interpolated + 0.5f);
        return constrain(rounded, 0, 100);
    }

    return 0;
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
