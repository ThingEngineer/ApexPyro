#ifndef CONTINUITY_H
#define CONTINUITY_H

#include <Arduino.h>
#include <array>
#include "config.h"

class ContinuityManager {
public:
    ContinuityManager();
    
    void begin();
    void update();
    
    // Get zone continuity status
    ContinuityStatus getZoneStatus(uint8_t zoneIdx);
    
    // Battery voltage
    float getBatteryVoltage();
    int getBatteryPercent();
    
    // Continuity thresholds (configurable via settings)
    float getLowGoodThreshold();
    float getHiGoodThreshold();
    float getLoOpenThreshold();
    
    void setThresholds(float loGood, float hiGood, float loOpen);
    
private:
    struct ResolvedBatteryProfile {
        BatteryProfile profile;
        uint8_t cellCount;
        float packMin;
        float packMax;
        float lowWarn;
        uint16_t sampleIntervalMs;
        uint8_t pointCount;
        BatteryCurvePoint points[BATTERY_MAX_CURVE_POINTS];
    };

    bool adsAvailable;
    std::array<bool, ADS1115_CONTINUITY_DEVICE_COUNT> adsAvailableByDevice;
    std::array<uint32_t, ADS1115_CONTINUITY_DEVICE_COUNT> lastAdsRecoveryAttemptMsByDevice;

    // Zone status array (MAX_ZONES total). Hardware scan path actively updates all 128 zones.
    ContinuityStatus zoneStatus[MAX_ZONES];
    uint32_t lastScanMs;
    uint32_t lastBatteryScanMs;
    
    // Current MUX scan state
    uint8_t currentMuxPosition;
    
    // Thresholds
    float threshLoGood;
    float threshHiGood;
    float threshLoOpen;

    float batteryVoltageRaw;
    float batteryVoltageFiltered;
    int batteryPercent;
    uint32_t lastBatteryConfigRefreshMs;
    uint32_t lastBatteryPercentCalcMs;
    ResolvedBatteryProfile batteryProfile;
    
    // Helper methods
    bool initializeAdsDevice(uint8_t deviceIdx, bool isRecoveryAttempt);
    bool probeAdsConnection(uint8_t deviceIdx);
    void handleAdsUnavailable(uint8_t deviceIdx, const char* reason);
    void setMuxPosition(uint8_t position);
    void scanAllZones();
    void readBatteryVoltage();
    void refreshBatteryProfile(bool force = false);
    void applyPresetProfile(BatteryProfile profileId, uint8_t overrideCellCount);
    int calculateBatteryPercent(float packVoltage) const;
    ContinuityStatus classifyVoltage(float voltage);
    float readAdcChannel(uint8_t deviceIdx, uint8_t channel);
};

extern ContinuityManager continuityManager;

#endif  // CONTINUITY_H
