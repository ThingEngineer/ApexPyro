#ifndef CONTINUITY_H
#define CONTINUITY_H

#include <Arduino.h>
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
    bool adsAvailable;

    // Zone status array (48 zones total)
    ContinuityStatus zoneStatus[MAX_ZONES];
    uint32_t lastScanMs;
    uint32_t lastBatteryScanMs;
    
    // Current MUX scan state
    uint8_t currentMuxPosition;
    
    // Thresholds
    float threshLoGood;
    float threshHiGood;
    float threshLoOpen;
    
    // Helper methods
    void setMuxPosition(uint8_t position);
    void scanAllZones();
    void readBatteryVoltage();
    ContinuityStatus classifyVoltage(float voltage);
    float readAdcChannel(uint8_t channel);
};

extern ContinuityManager continuityManager;

#endif  // CONTINUITY_H
