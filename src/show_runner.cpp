/*
 * Licensed under the Apache License 2.0. See LICENSE.
 * ApexPyro controls pyrotechnic hardware and is provided as-is, without warranty.
 * Compiling, flashing, and operating this software is entirely at your own risk.
 */

#include "show_runner.h"
#include "storage.h"
#include "relay_manager.h"
#include "websocket_handler.h"
#include "logger.h"
#include <algorithm>

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
    
    // Build queue from zones and groups
    buildQueueFromZones();
    
    if (queue.empty()) {
        Serial.println("[ShowRunner] Cannot start: No enabled zones with times");
        return false;
    }
    
    // Verify all enabled zones have valid time set
    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        if (!storage.isZoneEnabled(i)) {
            continue;
        }

        if (storage.getZoneTime(i) <= 0) {
            Serial.printf("[ShowRunner] Zone %u is enabled but has no time set\n", i);
            return false;
        }
    }
    
    state = ShowState::RUNNING;
    currentStepIdx = 0;
    stepStartMs = millis();
    
    Serial.printf("[ShowRunner] Show started with %u zones\n", queue.size());
    fireCurrentZone();
    wsHandler.broadcastShowState();
    
    return true;
}

void ShowRunner::buildQueueFromZones() {
    queue.clear();

    // Create ordered list of enabled zones based on their order value.
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

    bool groupedConsumed[MAX_ZONES] = {false};

    // Build queue
    for (const auto& item : orderedZones) {
        uint8_t zoneIdx = item.second;
        if (groupedConsumed[zoneIdx]) {
            continue;
        }

        float timeSpec = storage.getZoneTime(zoneIdx);

        if (timeSpec <= 0) {
            continue;
        }

        uint8_t groupId = storage.getZoneGroup(zoneIdx);
        if (groupId > 0) {
            ZoneQueueItem groupedItem;
            groupedItem.zoneIdx = zoneIdx;
            groupedItem.groupId = groupId;
            groupedItem.isGroupFire = true;
            groupedItem.timeSec = timeSpec;

            for (const auto& sibling : orderedZones) {
                uint8_t siblingIdx = sibling.second;
                if (groupedConsumed[siblingIdx] || !storage.isZoneEnabled(siblingIdx)) {
                    continue;
                }
                if (storage.getZoneGroup(siblingIdx) != groupId) {
                    continue;
                }

                float siblingTime = storage.getZoneTime(siblingIdx);
                if (siblingTime <= 0) {
                    continue;
                }

                groupedItem.zoneIndices.push_back(siblingIdx);
                groupedConsumed[siblingIdx] = true;
                if (siblingTime > groupedItem.timeSec) {
                    groupedItem.timeSec = siblingTime;
                }
            }

            if (!groupedItem.zoneIndices.empty()) {
                queue.push_back(groupedItem);
            }
            continue;
        }

        groupedConsumed[zoneIdx] = true;
        queue.push_back({zoneIdx, 0, {zoneIdx}, timeSpec, false});
    }
}

void ShowRunner::fireCurrentZone() {
    if (currentStepIdx >= queue.size()) {
        return;
    }
    
    const ZoneQueueItem& item = queue[currentStepIdx];
    uint16_t ignitersOnDuration = storage.getIgniterDuration();

    if (item.isGroupFire) {
        relayManager.startZonesFire(item.zoneIndices, ignitersOnDuration);
        uint32_t zoneDurationMs = static_cast<uint32_t>(item.timeSec * 1000.0f);
        if (zoneDurationMs == 0) { zoneDurationMs = 1; }
        for (size_t idx = 0; idx < item.zoneIndices.size(); idx++) {
            wsHandler.broadcastZoneFired(item.zoneIndices[idx], zoneDurationMs, static_cast<uint32_t>(ignitersOnDuration));
        }
        Serial.printf("[ShowRunner] Step %u: Group %u firing (%u zones, %.2fs)\n", currentStepIdx + 1, item.groupId, item.zoneIndices.size(), item.timeSec);
        wsHandler.broadcastShowProgress(currentStepIdx + 1, queue.size(), item.zoneIndices.front());
    } else {
        relayManager.startZoneFire(item.zoneIdx, ignitersOnDuration);
        Serial.printf("[ShowRunner] Step %u: Zone %u firing (%.2fs)\n", currentStepIdx + 1, item.zoneIdx, item.timeSec);
        wsHandler.broadcastShowProgress(currentStepIdx + 1, queue.size(), item.zoneIdx);
        uint32_t zoneDurationMs = static_cast<uint32_t>(item.timeSec * 1000.0f);
        if (zoneDurationMs == 0) { zoneDurationMs = 1; }
        wsHandler.broadcastZoneFired(item.zoneIdx, zoneDurationMs, static_cast<uint32_t>(ignitersOnDuration));
    }

    wsHandler.broadcastShowState();
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
        wsHandler.broadcastShowState();
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
    wsHandler.broadcastShowState();
    wsHandler.broadcastSystemStatus();
}

void ShowRunner::onShowAborted() {
    state = ShowState::ABORTED;
    currentStepIdx = 0;
    queue.clear();
    
    Serial.println("[ShowRunner] Show aborted");
    wsHandler.broadcastShowState();
    wsHandler.broadcastSystemStatus();
}
