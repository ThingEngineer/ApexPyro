#ifndef RELAY_MANAGER_H
#define RELAY_MANAGER_H

#include <Arduino.h>
#include <vector>
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
    void setAuxRelay(uint8_t relayIdx, bool state);  // 0..AUX_RELAY_COUNT-1
    bool getAuxRelayState(uint8_t relayIdx);
    
    // Zone Firing
    void startZoneFire(uint8_t zoneIdx, uint32_t durationMs);
    void startZonesFire(const std::vector<uint8_t>& zoneIndices, uint32_t durationMs);
    void stopZoneFire();
    bool isZoneFiring();
    uint8_t getFiringZoneIdx();
    
    // Direct relay control (used by main firing logic)
    void setAllRelaysOff();
    
    // Board detection (populated by main.cpp)
    bool boardPresent[MAX_BOARDS];
    uint8_t boardPresentCount;
    
    // Callback for when zone firing completes
    void onZoneFireComplete();

    // Set after each auto-off; cleared by caller (wsHandler)
    bool consumeFireComplete();

private:
    bool fireJustCompleted;
    bool masterArmed;
    bool auxState[AUX_RELAY_COUNT];
    
    // Firing state
    uint8_t firingZoneIdx;
    uint8_t firingZoneCount;
    bool firingZones[MAX_ZONES];
    uint32_t zoneFireUntilMs[MAX_ZONES];
    bool isFiring;

    uint8_t boardPort0Mask[MAX_BOARDS];
    uint8_t boardPort1Mask[MAX_BOARDS];

    void rebuildOutputMasksFromActiveZones();
    void applyOutputMasks();
    void clearAllActiveZones();
};

extern RelayManager relayManager;

#endif  // RELAY_MANAGER_H
