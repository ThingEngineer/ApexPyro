#include "relay_manager.h"

RelayManager relayManager;

RelayManager::RelayManager()
    : masterArmed(false), isFiring(false), firingZoneIdx(0), 
      firingStartMs(0), firingDurationMs(0), boardPresentCount(0) {
    auxState[0] = false;
    auxState[1] = false;
}

void RelayManager::begin() {
    // GPIO pins already initialized in main.cpp boot safety
    setMasterArm(false);
    setAuxRelay(0, false);
    setAuxRelay(1, false);
    setAllRelaysOff();
    
    Serial.println("[RelayManager] Initialized");
}

void RelayManager::update() {
    // Check if current zone fire duration has elapsed
    if (isFiring && firingDurationMs > 0) {
        uint32_t elapsedMs = millis() - firingStartMs;
        if (elapsedMs >= firingDurationMs) {
            stopZoneFire();
            onZoneFireComplete();
        }
    }
}

void RelayManager::setMasterArm(bool armed) {
    masterArmed = armed;
    digitalWrite(MASTER_ARM_RELAY_PIN, armed ? HIGH : LOW);
    Serial.printf("[RelayManager] Master Arm: %s\n", armed ? "ARMED" : "DISARMED");
}

bool RelayManager::isMasterArmed() {
    return masterArmed;
}

void RelayManager::setAuxRelay(uint8_t relayIdx, bool state) {
    if (relayIdx > 1) return;
    
    auxState[relayIdx] = state;
    int pin = (relayIdx == 0) ? AUX_RELAY_1_PIN : AUX_RELAY_2_PIN;
    digitalWrite(pin, state ? HIGH : LOW);
    
    Serial.printf("[RelayManager] Aux Relay %u: %s\n", relayIdx, state ? "ON" : "OFF");
}

bool RelayManager::getAuxRelayState(uint8_t relayIdx) {
    if (relayIdx > 1) return false;
    return auxState[relayIdx];
}

void RelayManager::startZoneFire(uint8_t zoneIdx, uint32_t durationMs) {
    if (!masterArmed) {
        Serial.println("[RelayManager] Cannot fire: Master Arm not engaged");
        return;
    }
    
    firingZoneIdx = zoneIdx;
    firingStartMs = millis();
    firingDurationMs = durationMs;
    isFiring = true;
    
    // Activate the relay for this zone
    setSingleZone(zoneIdx);
    
    Serial.printf("[RelayManager] Zone %u firing for %lu ms\n", zoneIdx, durationMs);
}

void RelayManager::stopZoneFire() {
    if (isFiring) {
        setAllRelaysOff();
        isFiring = false;
        Serial.printf("[RelayManager] Zone %u fire complete\n", firingZoneIdx);
    }
}

bool RelayManager::isZoneFiring() {
    return isFiring;
}

uint8_t RelayManager::getFiringZoneIdx() {
    return firingZoneIdx;
}

void RelayManager::onZoneFireComplete() {
    // This is called when a fire sequence completes
    // Upper layers (show runner, UI) can use this for state updates
}

// ============================================================================
// Direct Relay Control (integrates with main.cpp functions)
// ============================================================================

// Forward declarations of functions in main.cpp
extern bool writeRegister(uint8_t address, uint8_t reg, uint8_t value);
extern void setAllRelaysOffOnBoard(uint8_t address);
extern void setSingleRelayOnBoard(uint8_t address, uint8_t relayIndex);

void RelayManager::setSingleZone(uint8_t zoneIndex) {
    uint8_t boardIndex = zoneIndex / RELAYS_PER_BOARD;
    uint8_t relayIndex = zoneIndex % RELAYS_PER_BOARD;
    
    setAllRelaysOff();
    
    if (boardIndex < boardPresentCount && boardPresent[boardIndex]) {
        setSingleRelayOnBoard(BOARD_ADDRS[boardIndex], relayIndex);
    }
}

void RelayManager::setAllRelaysOff() {
    for (uint8_t i = 0; i < boardPresentCount; i++) {
        if (boardPresent[i]) {
            setAllRelaysOffOnBoard(BOARD_ADDRS[i]);
        }
    }
}
