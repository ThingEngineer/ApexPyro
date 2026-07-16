/*
 * Licensed under the Apache License 2.0. See LICENSE.
 * ApexPyro controls pyrotechnic hardware and is provided as-is, without warranty.
 * Compiling, flashing, and operating this software is entirely at your own risk.
 */

#ifndef SHOW_RUNNER_H
#define SHOW_RUNNER_H

#include <Arduino.h>
#include "config.h"
#include <vector>

enum class ShowState : uint8_t {
    IDLE = 0,
    RUNNING = 1,
    ABORTED = 2,
};

struct ZoneQueueItem {
    uint8_t zoneIdx;
    uint8_t groupId;
    std::vector<uint8_t> zoneIndices;
    float timeSec;
    bool isGroupFire;  // If true, zoneIdx points to a group
};

class ShowRunner {
public:
    ShowRunner();
    
    void begin();
    void update();
    
    // Show Control
    bool startShow();
    void abortShow();
    void stopShow();
    
    // Status
    ShowState getShowState();
    bool isShowRunning();
    uint8_t getCurrentStepIdx();
    uint8_t getTotalSteps();
    
    // Configuration
    void setAbortOnClientDisconnect(bool flag);
    
private:
    ShowState state;
    std::vector<ZoneQueueItem> queue;
    uint8_t currentStepIdx;
    uint32_t stepStartMs;
    bool abortOnClientDisconnect;
    
    // Helper methods
    void buildQueueFromZones();
    void fireCurrentZone();
    void advanceToNextZone();
    void onShowComplete();
    void onShowAborted();
};

extern ShowRunner showRunner;

#endif  // SHOW_RUNNER_H
