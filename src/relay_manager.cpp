#include "relay_manager.h"

#include "logger.h"

namespace {
static const uint8_t REG_OUTPUT_PORT0 = 0x02;
static const uint8_t REG_OUTPUT_PORT1 = 0x03;
}

// Forward declarations of functions in main.cpp
extern bool writeRegister(uint8_t address, uint8_t reg, uint8_t value);
extern void setAllRelaysOffOnBoard(uint8_t address);
extern void setAllAuxRelaysOffOnBoard();
extern void setAuxRelayOnBoard(uint8_t relayIdx, bool state);

RelayManager relayManager;

RelayManager::RelayManager()
    : masterArmed(false), isFiring(false), firingZoneIdx(0),
      firingZoneCount(0), boardPresentCount(0),
      fireJustCompleted(false) {
    for (uint8_t i = 0; i < MAX_BOARDS; i++) {
        boardPresent[i] = false;
    }
    for (uint8_t relayIdx = 0; relayIdx < AUX_RELAY_COUNT; relayIdx++) {
        auxState[relayIdx] = false;
    }
    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        firingZones[i] = false;
        zoneFireUntilMs[i] = 0;
    }
    for (uint8_t i = 0; i < MAX_BOARDS; i++) {
        boardPort0Mask[i] = 0;
        boardPort1Mask[i] = 0;
    }
}

void RelayManager::begin() {
    // Master-arm safety pin is initialized in main.cpp boot safety.
    // Auxiliary relays now route through dedicated AUX I2C PW535 board.
    setMasterArm(false);
    for (uint8_t relayIdx = 0; relayIdx < AUX_RELAY_COUNT; relayIdx++) {
        setAuxRelay(relayIdx, false);
    }
    setAllRelaysOff();
    
    Serial.println("[RelayManager] Initialized");
}

void RelayManager::update() {
    if (!isFiring) {
        return;
    }

    uint32_t now = millis();
    bool anyCompleted = false;
    bool outputsChanged = false;
    uint8_t activeCount = 0;
    uint8_t firstActiveZone = firingZoneIdx;

    for (uint8_t zoneIdx = 0; zoneIdx < MAX_ZONES; zoneIdx++) {
        if (!firingZones[zoneIdx]) {
            continue;
        }

        if ((int32_t)(now - zoneFireUntilMs[zoneIdx]) >= 0) {
            firingZones[zoneIdx] = false;
            zoneFireUntilMs[zoneIdx] = 0;
            outputsChanged = true;
            anyCompleted = true;
            Serial.printf("[RelayManager] Zone %u fire complete\n", zoneIdx);
            continue;
        }

        if (activeCount == 0) {
            firstActiveZone = zoneIdx;
        }
        activeCount++;
    }

    firingZoneCount = activeCount;
    isFiring = (activeCount > 0);
    if (isFiring) {
        firingZoneIdx = firstActiveZone;
    }

    if (outputsChanged) {
        rebuildOutputMasksFromActiveZones();
        applyOutputMasks();
    }

    if (anyCompleted) {
        onZoneFireComplete();
    }
}

void RelayManager::setMasterArm(bool armed) {
    if (!armed) {
        stopZoneFire();
    }

    masterArmed = armed;
    digitalWrite(MASTER_ARM_RELAY_PIN, armed ? HIGH : LOW);
    Serial.printf("[RelayManager] Master Arm: %s\n", armed ? "ARMED" : "DISARMED");
}

bool RelayManager::isMasterArmed() {
    return masterArmed;
}

void RelayManager::setAuxRelay(uint8_t relayIdx, bool state) {
    if (relayIdx >= AUX_RELAY_COUNT) return;
    
    auxState[relayIdx] = state;
    setAuxRelayOnBoard(static_cast<uint8_t>(AUX_BOARD_BASE_RELAY + relayIdx), state);
    
    Serial.printf("[RelayManager] Aux Relay %u: %s\n", relayIdx, state ? "ON" : "OFF");
}

bool RelayManager::getAuxRelayState(uint8_t relayIdx) {
    if (relayIdx >= AUX_RELAY_COUNT) return false;
    return auxState[relayIdx];
}

void RelayManager::startZoneFire(uint8_t zoneIdx, uint32_t durationMs) {
    if (!masterArmed) {
        Serial.println("[RelayManager] Cannot fire: Master Arm not engaged");
        return;
    }

    if (zoneIdx >= MAX_ZONES) {
        Serial.printf("[RelayManager] Cannot fire: Zone %u out of range\n", zoneIdx);
        return;
    }
    
    uint32_t safeDurationMs = durationMs == 0 ? 1 : durationMs;
    uint32_t now = millis();

    if (!firingZones[zoneIdx]) {
        firingZoneCount++;
    }
    firingZones[zoneIdx] = true;
    zoneFireUntilMs[zoneIdx] = now + safeDurationMs;
    firingZoneIdx = zoneIdx;
    isFiring = true;

    rebuildOutputMasksFromActiveZones();
    applyOutputMasks();
    
    Serial.printf("[RelayManager] Zone %u firing for %lu ms\n", zoneIdx, safeDurationMs);
}

void RelayManager::startZonesFire(const std::vector<uint8_t>& zoneIndices, uint32_t durationMs) {
    if (!masterArmed) {
        Serial.println("[RelayManager] Cannot fire group: Master Arm not engaged");
        return;
    }

    if (zoneIndices.empty()) {
        return;
    }

    uint32_t safeDurationMs = durationMs == 0 ? 1 : durationMs;
    uint32_t now = millis();
    uint8_t validCount = 0;
    uint8_t firstZone = 0;
    bool firstZoneSet = false;

    for (uint8_t zoneIdx : zoneIndices) {
        if (zoneIdx >= MAX_ZONES) {
            continue;
        }

        uint8_t boardIndex = zoneIdx / RELAYS_PER_BOARD;
        if (boardIndex >= boardPresentCount || !boardPresent[boardIndex]) {
            continue;
        }

        if (!firingZones[zoneIdx]) {
            firingZones[zoneIdx] = true;
            firingZoneCount++;
        }

        zoneFireUntilMs[zoneIdx] = now + safeDurationMs;
        validCount++;
        if (!firstZoneSet) {
            firstZone = zoneIdx;
            firstZoneSet = true;
        }
    }

    if (validCount == 0) {
        Serial.println("[RelayManager] Cannot fire group: no valid zones");
        return;
    }

    rebuildOutputMasksFromActiveZones();
    applyOutputMasks();

    firingZoneIdx = firstZone;
    isFiring = true;

    Serial.printf("[RelayManager] Group firing %u zone(s) for %lu ms\n", validCount, safeDurationMs);
}

void RelayManager::stopZoneFire() {
    if (!isFiring) {
        return;
    }

    clearAllActiveZones();
    setAllRelaysOff();
    isFiring = false;
    firingZoneCount = 0;
}

bool RelayManager::isZoneFiring() {
    return isFiring;
}

uint8_t RelayManager::getFiringZoneIdx() {
    return firingZoneIdx;
}

void RelayManager::onZoneFireComplete() {
    fireJustCompleted = true;
}

bool RelayManager::consumeFireComplete() {
    if (fireJustCompleted) {
        fireJustCompleted = false;
        return true;
    }
    return false;
}

// ============================================================================
// Direct Relay Control (integrates with main.cpp functions)
// ============================================================================

void RelayManager::setAllRelaysOff() {
    for (uint8_t i = 0; i < boardPresentCount; i++) {
        if (boardPresent[i]) {
            setAllRelaysOffOnBoard(BOARD_ADDRS[i]);
        }
    }
}

void RelayManager::rebuildOutputMasksFromActiveZones() {
    for (uint8_t boardIdx = 0; boardIdx < MAX_BOARDS; boardIdx++) {
        boardPort0Mask[boardIdx] = 0;
        boardPort1Mask[boardIdx] = 0;
    }

    for (uint8_t zoneIdx = 0; zoneIdx < MAX_ZONES; zoneIdx++) {
        if (!firingZones[zoneIdx]) {
            continue;
        }

        uint8_t boardIndex = zoneIdx / RELAYS_PER_BOARD;
        uint8_t relayIndex = zoneIdx % RELAYS_PER_BOARD;
        if (boardIndex >= MAX_BOARDS) {
            continue;
        }

        if (relayIndex < 8) {
            boardPort0Mask[boardIndex] |= static_cast<uint8_t>(1U << relayIndex);
        } else {
            boardPort1Mask[boardIndex] |= static_cast<uint8_t>(1U << (relayIndex - 8));
        }
    }
}

void RelayManager::applyOutputMasks() {
    for (uint8_t boardIdx = 0; boardIdx < boardPresentCount; boardIdx++) {
        if (!boardPresent[boardIdx]) {
            continue;
        }

        writeRegister(BOARD_ADDRS[boardIdx], REG_OUTPUT_PORT0, boardPort0Mask[boardIdx]);
        writeRegister(BOARD_ADDRS[boardIdx], REG_OUTPUT_PORT1, boardPort1Mask[boardIdx]);
    }
}

void RelayManager::clearAllActiveZones() {
    for (uint8_t zoneIdx = 0; zoneIdx < MAX_ZONES; zoneIdx++) {
        firingZones[zoneIdx] = false;
        zoneFireUntilMs[zoneIdx] = 0;
    }
}
