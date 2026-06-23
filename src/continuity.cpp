#include "continuity.h"
#include "storage.h"
#include "logger.h"
#include "relay_manager.h"
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// Continuity/battery ADC instances on the auxiliary I2C bus
std::array<Adafruit_ADS1115, ADS1115_CONTINUITY_DEVICE_COUNT> adsDevices;

ContinuityManager continuityManager;

namespace {
static const uint32_t ADS1115_RECOVERY_RETRY_MS = 1000;
}

ContinuityManager::ContinuityManager()
        : adsAvailable(false), adsAvailableByDevice{false, false, false}, lastAdsRecoveryAttemptMsByDevice{0, 0, 0},
            lastScanMs(0), lastBatteryScanMs(0), currentMuxPosition(0),
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

    uint32_t now = millis();
    for (uint8_t i = 0; i < ADS1115_CONTINUITY_DEVICE_COUNT; i++) {
        lastAdsRecoveryAttemptMsByDevice[i] = now;
        initializeAdsDevice(i, false);
    }

    adsAvailable = adsAvailableByDevice[0] || adsAvailableByDevice[1] || adsAvailableByDevice[2];

    Serial.printf("[ContinuityManager] Continuity thresholds: lo_good=%.4fV, hi_good=%.4fV, lo_open=%.4fV\n",
                  threshLoGood, threshHiGood, threshLoOpen);
}

void ContinuityManager::update() {
    uint32_t now = millis();

    for (uint8_t i = 0; i < ADS1115_CONTINUITY_DEVICE_COUNT; i++) {
        if (!adsAvailableByDevice[i]) {
            if (now - lastAdsRecoveryAttemptMsByDevice[i] >= ADS1115_RECOVERY_RETRY_MS) {
                lastAdsRecoveryAttemptMsByDevice[i] = now;
                initializeAdsDevice(i, true);
            }
            continue;
        }

        if (!probeAdsConnection(i)) {
            handleAdsUnavailable(i, "communication lost");
        }
    }

    adsAvailable = adsAvailableByDevice[0] || adsAvailableByDevice[1] || adsAvailableByDevice[2];

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

bool ContinuityManager::initializeAdsDevice(uint8_t deviceIdx, bool isRecoveryAttempt) {
    if (deviceIdx >= ADS1115_CONTINUITY_DEVICE_COUNT) {
        return false;
    }

    uint8_t address = ADS1115_CONTINUITY_ADDRS[deviceIdx];
    if (!adsDevices[deviceIdx].begin(address, &auxWire)) {
        adsAvailableByDevice[deviceIdx] = false;
        if (!isRecoveryAttempt) {
            Serial.printf("[ContinuityManager] ADS1115 not found at 0x%02X; continuity lane %u unavailable\n",
                          address, static_cast<unsigned int>(deviceIdx));
        }
        return false;
    }

    adsDevices[deviceIdx].setGain(GAIN_ONE);
    adsAvailableByDevice[deviceIdx] = true;

    if (isRecoveryAttempt) {
        Serial.printf("[ContinuityManager] ADS1115 communication restored at 0x%02X\n", address);
    } else {
        Serial.printf("[ContinuityManager] ADS1115 initialized at 0x%02X\n", address);
    }

    return true;
}

bool ContinuityManager::probeAdsConnection(uint8_t deviceIdx) {
    if (deviceIdx >= ADS1115_CONTINUITY_DEVICE_COUNT) {
        return false;
    }

    auxWire.beginTransmission(ADS1115_CONTINUITY_ADDRS[deviceIdx]);
    return auxWire.endTransmission() == 0;
}

void ContinuityManager::handleAdsUnavailable(uint8_t deviceIdx, const char* reason) {
    if (deviceIdx >= ADS1115_CONTINUITY_DEVICE_COUNT || !adsAvailableByDevice[deviceIdx]) {
        return;
    }

    adsAvailableByDevice[deviceIdx] = false;
    lastAdsRecoveryAttemptMsByDevice[deviceIdx] = millis();
    Serial.printf("[ContinuityManager] ADS1115 %s at 0x%02X; continuity lane %u paused\n",
                  reason, ADS1115_CONTINUITY_ADDRS[deviceIdx], static_cast<unsigned int>(deviceIdx));
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

float ContinuityManager::readAdcChannel(uint8_t deviceIdx, uint8_t channel) {
    if (deviceIdx >= ADS1115_CONTINUITY_DEVICE_COUNT || !adsAvailableByDevice[deviceIdx]) {
        return 0.0f;
    }

    if (channel > 3) return 0.0f;
    
    int16_t adc = adsDevices[deviceIdx].readADC_SingleEnded(channel);
    // Convert ADC value to voltage
    // With GAIN_ONE (±4.096V), each count is 0.125mV
    float voltage = adc * 0.125f / 1000.0f;  // Convert mV to V
    return voltage;
}

void ContinuityManager::scanAllZones() {
    // Scan all 16 MUX positions.
    // Each position gives us 8 zone readings (from 8 MUXes shared across 2 ADS1115 devices).
    for (uint8_t mux_pos = 0; mux_pos < 16; mux_pos++) {
        setMuxPosition(mux_pos);
        
        for (uint8_t lane = 0; lane < ADS1115_CONTINUITY_LANE_COUNT; lane++) {
            uint8_t deviceIdx = lane / ADS1115_CONTINUITY_LANES_PER_DEVICE;
            uint8_t channel = lane % ADS1115_CONTINUITY_LANES_PER_DEVICE;
            uint8_t zoneIdx = mux_pos + (lane * ADS1115_CONTINUITY_ZONE_SPAN_PER_LANE);

            float voltage = readAdcChannel(deviceIdx, channel);
            if (zoneIdx < MAX_ZONES) {
                zoneStatus[zoneIdx] = classifyVoltage(voltage);
            }
        }
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
    // Read from ADS1115 #3 channel 0 (battery voltage divider)
    float adcVolt = readAdcChannel(ADS1115_CONTINUITY_BATTERY_DEVICE_INDEX, 0);
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
    if (!adsAvailableByDevice[ADS1115_CONTINUITY_BATTERY_DEVICE_INDEX]) {
        return 0.0f;
    }

    if (batteryVoltageFiltered <= 0.0f) {
        readBatteryVoltage();
    }

    return batteryVoltageFiltered;
}

int ContinuityManager::getBatteryPercent() {
    if (!adsAvailableByDevice[ADS1115_CONTINUITY_BATTERY_DEVICE_INDEX]) {
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
