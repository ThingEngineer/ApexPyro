#include "show_runner.h"
#include "storage.h"
#include "relay_manager.h"
#include "websocket_handler.h"

ShowRunner showRunner;

ShowRunner::ShowRunner()
    : state(ShowState::IDLE), currentStepIdx(0), stepStartMs(0), abortOnClientDisconnect(false) {
}

void ShowRunner::begin() {
    abortOnClientDisconnect = storage.getAbortOnDisconnect();
    Serial.println("[ShowRunner] Initialized");
}

void ShowRunner::update() {
    if (state != ShowState::RUNNING) {
        return;
    }
    
    uint32_t now = millis();
    uint32_t elapsedMs = now - stepStartMs;
    
    if (currentStepIdx >= queue.size()) {
        onShowComplete();
        return;
    }
    
    const ZoneQueueItem& item = queue[currentStepIdx];
    uint32_t zoneDurationMs = (uint32_t)(item.timeSec * 1000.0f);
    uint32_t delayMs = (uint32_t)(storage.getAutoDelay() * 1000);
    uint32_t totalDelayForStep = zoneDurationMs + delayMs;
    
    if (elapsedMs >= totalDelayForStep) {
        advanceToNextZone();
    }
}

bool ShowRunner::startShow() {
    // Pre-flight checks
    if (!relayManager.isMasterArmed()) {
        Serial.println("[ShowRunner] Cannot start: Master Arm not engaged");
        return false;
    }
    
    // Build queue from zones
    buildQueueFromZones();
    
    if (queue.empty()) {
        Serial.println("[ShowRunner] Cannot start: No enabled zones with times");
        return false;
    }
    
    // Verify all enabled zones have time set
    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        if (storage.isZoneEnabled(i) && storage.getZoneTime(i) <= 0) {
            Serial.printf("[ShowRunner] Zone %u is enabled but has no time set\n", i);
            return false;
        }
    }
    
    state = ShowState::RUNNING;
    currentStepIdx = 0;
    stepStartMs = millis();
    
    Serial.printf("[ShowRunner] Show started with %u zones\n", queue.size());
    fireCurrentZone();
    
    return true;
}

void ShowRunner::buildQueueFromZones() {
    queue.clear();
    
    // Create ordered list of enabled zones based on their order value
    std::vector<std::pair<uint8_t, uint8_t>> orderedZones;  // (order, zoneIdx)
    
    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        if (storage.isZoneEnabled(i)) {
            uint8_t order = storage.getZoneOrder(i);
            orderedZones.push_back({order, i});
        }
    }
    
    // Sort by order value
    std::sort(orderedZones.begin(), orderedZones.end(),
              [](const std::pair<uint8_t, uint8_t>& a, const std::pair<uint8_t, uint8_t>& b) { 
                  return a.first < b.first; 
              });
    
    // Build queue
    for (const auto& item : orderedZones) {
        uint8_t zoneIdx = item.second;
        float timeSpec = storage.getZoneTime(zoneIdx);
        
        if (timeSpec > 0) {
            queue.push_back({zoneIdx, timeSpec, false});
        }
    }
}

void ShowRunner::fireCurrentZone() {
    if (currentStepIdx >= queue.size()) {
        return;
    }
    
    const ZoneQueueItem& item = queue[currentStepIdx];
    uint16_t ignitersOnDuration = storage.getIgniterDuration();
    
    relayManager.startZoneFire(item.zoneIdx, ignitersOnDuration);
    
    Serial.printf("[ShowRunner] Step %u: Zone %u firing (%.2fs)\n", currentStepIdx + 1, item.zoneIdx, item.timeSec);
    wsHandler.broadcastShowProgress(currentStepIdx + 1, queue.size(), item.zoneIdx);
}

void ShowRunner::advanceToNextZone() {
    currentStepIdx++;
    
    if (currentStepIdx >= queue.size()) {
        onShowComplete();
    } else {
        stepStartMs = millis();
        fireCurrentZone();
    }
}

void ShowRunner::abortShow() {
    if (state == ShowState::RUNNING) {
        relayManager.setMasterArm(false);
        relayManager.setAllRelaysOff();
        onShowAborted();
    }
}

void ShowRunner::stopShow() {
    if (state == ShowState::RUNNING) {
        state = ShowState::IDLE;
        currentStepIdx = 0;
        queue.clear();
        Serial.println("[ShowRunner] Show stopped");
    }
}

ShowState ShowRunner::getShowState() {
    return state;
}

bool ShowRunner::isShowRunning() {
    return state == ShowState::RUNNING;
}

uint8_t ShowRunner::getCurrentStepIdx() {
    return currentStepIdx;
}

uint8_t ShowRunner::getTotalSteps() {
    return queue.size();
}

void ShowRunner::setAbortOnClientDisconnect(bool flag) {
    abortOnClientDisconnect = flag;
    storage.setAbortOnDisconnect(flag);
}

void ShowRunner::onShowComplete() {
    state = ShowState::IDLE;
    currentStepIdx = 0;
    queue.clear();
    
    Serial.println("[ShowRunner] Show complete");
    wsHandler.broadcastSystemStatus();
}

void ShowRunner::onShowAborted() {
    state = ShowState::ABORTED;
    currentStepIdx = 0;
    queue.clear();
    
    Serial.println("[ShowRunner] Show aborted");
    wsHandler.broadcastSystemStatus();
}
