#ifndef RELAY_MANAGER_H
#define RELAY_MANAGER_H

#include <Arduino.h>
#include "config.h"

class RelayManager {
public:
    RelayManager();
    
    void begin();
    void update();
    
    // Master Arm Control (igniter power bus)
    void setMasterArm(bool armed);
    bool isMasterArmed();
    
    // Aux Relay Control
    void setAuxRelay(uint8_t relayIdx, bool state);  // 0 or 1
    bool getAuxRelayState(uint8_t relayIdx);
    
    // Zone Firing
    void startZoneFire(uint8_t zoneIdx, uint32_t durationMs);
    void stopZoneFire();
    bool isZoneFiring();
    uint8_t getFiringZoneIdx();
    
    // Direct relay control (used by main firing logic)
    void setSingleZone(uint8_t zoneIndex);
    void setAllRelaysOff();
    
    // Board detection (populated by main.cpp)
    bool boardPresent[MAX_BOARDS];
    uint8_t boardPresentCount;
    
    // Callback for when zone firing completes
    void onZoneFireComplete();
    
private:
    bool masterArmed;
    bool auxState[2];  // AUX relay 1 and 2 states
    
    // Firing state
    uint8_t firingZoneIdx;
    uint32_t firingStartMs;
    uint32_t firingDurationMs;
    bool isFiring;
};

extern RelayManager relayManager;

#endif  // RELAY_MANAGER_H
