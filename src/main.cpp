#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "storage.h"
#include "wifi_manager.h"
#include "relay_manager.h"
#include "continuity.h"
#include "websocket_handler.h"
#include "show_runner.h"
#include "logger.h"

// ============================================================================
// EXISTING RELAY BOARD CODE (Preserved)
// ============================================================================

static uint8_t boardPresentCount = 0;
static bool boardPresent[3] = {false, false, false};
static bool boardCommHealthy[MAX_BOARDS] = {false, false, false};
static bool killSwitchStableState = false;
static bool killSwitchRawState = false;
static uint32_t killSwitchLastTransitionMs = 0;
static bool i2cRuntimeDiagnosticsEnabled = false;

// Relay board register constants
static const uint8_t REG_OUTPUT_PORT0 = 0x02;  // Relays A0-A7
static const uint8_t REG_OUTPUT_PORT1 = 0x03;  // Relays B0-B7
static const uint8_t REG_CONFIG_PORT0 = 0x06;  // 0 = output, 1 = input
static const uint8_t REG_CONFIG_PORT1 = 0x07;  // 0 = output, 1 = input

int8_t getBoardIndexForAddress(uint8_t address) {
  for (uint8_t i = 0; i < MAX_BOARDS; i++) {
    if (BOARD_ADDRS[i] == address) {
      return static_cast<int8_t>(i);
    }
  }

  return -1;
}

bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  uint8_t errorCode = Wire.endTransmission();

  if (i2cRuntimeDiagnosticsEnabled) {
    int8_t boardIndex = getBoardIndexForAddress(address);
    if (boardIndex >= 0) {
      bool isHealthy = (errorCode == 0);
      bool wasHealthy = boardCommHealthy[boardIndex];

      if (!isHealthy && wasHealthy) {
        Serial.printf("[I2C] PW535 board 0x%02X write failed (reg=0x%02X, err=%u)\n", address, reg, errorCode);
      } else if (isHealthy && !wasHealthy) {
        Serial.printf("[I2C] PW535 board 0x%02X communication restored\n", address);
      }

      boardCommHealthy[boardIndex] = isHealthy;
    }
  }

  return errorCode == 0;
}

void setAllRelaysOffOnBoard(uint8_t address) {
  writeRegister(address, REG_OUTPUT_PORT0, 0x00);
  writeRegister(address, REG_OUTPUT_PORT1, 0x00);
}

void setAllRelaysOff() {
  for (uint8_t i = 0; i < MAX_BOARDS; i++) {
    if (boardPresent[i]) {
      setAllRelaysOffOnBoard(BOARD_ADDRS[i]);
    }
  }
}

bool initPw535(uint8_t address) {
  bool ok0 = writeRegister(address, REG_CONFIG_PORT0, 0x00);
  bool ok1 = writeRegister(address, REG_CONFIG_PORT1, 0x00);
  setAllRelaysOffOnBoard(address);
  return ok0 && ok1;
}

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("\n\n========================================");
  Serial.println("ApexPyro Fireworks Controller");
  Serial.println("Booting...");
  Serial.println("========================================\n");

  // ========================================================================
  // CRITICAL: Boot Safety — Set GPIO states BEFORE I2C init
  // ========================================================================
  Serial.println("[BOOT] Setting safety GPIO states...");
  
  // Master Arm Relay (GPIO 25) - Active HIGH, start LOW (safe)
  pinMode(MASTER_ARM_RELAY_PIN, OUTPUT);
  digitalWrite(MASTER_ARM_RELAY_PIN, LOW);
  
  // Aux Relay 1 (GPIO 26) - Active HIGH, start LOW (safe)
  pinMode(AUX_RELAY_1_PIN, OUTPUT);
  digitalWrite(AUX_RELAY_1_PIN, LOW);
  
  // Aux Relay 2 (GPIO 27) - Active HIGH, start LOW (safe)
  pinMode(AUX_RELAY_2_PIN, OUTPUT);
  digitalWrite(AUX_RELAY_2_PIN, LOW);

  // Aux Relay 3 (GPIO 32) - Active HIGH, start LOW (safe)
  pinMode(AUX_RELAY_3_PIN, OUTPUT);
  digitalWrite(AUX_RELAY_3_PIN, LOW);
  
  // MUX Select Pins (GPIO 16-19) - all outputs, start LOW
  pinMode(MUX_S0_PIN, OUTPUT);
  pinMode(MUX_S1_PIN, OUTPUT);
  pinMode(MUX_S2_PIN, OUTPUT);
  pinMode(MUX_S3_PIN, OUTPUT);
  digitalWrite(MUX_S0_PIN, LOW);
  digitalWrite(MUX_S1_PIN, LOW);
  digitalWrite(MUX_S2_PIN, LOW);
  digitalWrite(MUX_S3_PIN, LOW);

  // Physical kill switch input (active HIGH)
  pinMode(KILL_SWITCH_PIN, INPUT);
  killSwitchStableState = (digitalRead(KILL_SWITCH_PIN) == HIGH);
  killSwitchRawState = killSwitchStableState;
  killSwitchLastTransitionMs = millis();
  
  Serial.println("[BOOT] Safety GPIO states confirmed");
  delay(100);

  // ========================================================================
  // Initialize I2C & Relay Boards
  // ========================================================================
  Serial.println("[BOOT] Initializing I2C...");
  Wire.setPins(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.begin();
  Wire.setClock(100000);
  Serial.println("[BOOT] I2C initialized");

  Serial.println("[BOOT] Initializing PW535 relay boards...");
  boardPresentCount = 0;
  for (uint8_t i = 0; i < MAX_BOARDS; i++) {
    uint8_t address = BOARD_ADDRS[i];
    if (initPw535(address)) {
      boardPresent[i] = true;
      boardCommHealthy[i] = true;
      boardPresentCount++;
      Serial.printf("[BOOT] PW535 initialized at 0x%02X ✓\n", address);
    } else {
      boardPresent[i] = false;
      boardCommHealthy[i] = false;
      Serial.printf("[BOOT] PW535 init failed at 0x%02X ✗\n", address);
    }
  }
  Serial.printf("[BOOT] %u relay board(s) detected\n", boardPresentCount);
  i2cRuntimeDiagnosticsEnabled = true;

  // ========================================================================
  // Initialize Storage (NVS)
  // ========================================================================
  Serial.println("[BOOT] Initializing storage (NVS)...");
  storage.begin();
  Serial.println("[BOOT] Storage ready");

  // ========================================================================
  // Initialize WiFi Manager
  // ========================================================================
  Serial.println("[BOOT] Initializing WiFi...");
  wifiManager.begin();
  Serial.println("[BOOT] WiFi ready");

  // ========================================================================
  // TODO: Initialize remaining modules
  // ========================================================================
  Serial.println("[BOOT] Initializing Continuity Manager...");
  continuityManager.begin();
  Serial.println("[BOOT] Continuity Manager ready");
  
  Serial.println("[BOOT] Initializing Relay Manager...");
  relayManager.boardPresentCount = boardPresentCount;
  for (uint8_t i = 0; i < MAX_BOARDS; i++) {
    relayManager.boardPresent[i] = boardPresent[i];
  }
  relayManager.begin();
  Serial.println("[BOOT] Relay Manager ready");
  
  Serial.println("[BOOT] Initializing Show Runner...");
  showRunner.begin();
  Serial.println("[BOOT] Show Runner ready");
  
  Serial.println("[BOOT] Initializing WebSocket Handler...");
  wsHandler.begin();
  Serial.println("[BOOT] WebSocket Handler ready");

  Serial.println("\n[BOOT] ApexPyro startup complete!");
  Serial.println("Access at: http://apexpyro.local");
  Serial.println("========================================\n");
}

// ============================================================================
// LOOP
// ============================================================================

void loop() {
  // Debounced physical kill switch handling.
  bool rawState = (digitalRead(KILL_SWITCH_PIN) == HIGH);
  if (rawState != killSwitchRawState) {
    killSwitchRawState = rawState;
    killSwitchLastTransitionMs = millis();
  }

  if ((millis() - killSwitchLastTransitionMs) >= KILL_SWITCH_DEBOUNCE_MS && killSwitchStableState != killSwitchRawState) {
    killSwitchStableState = killSwitchRawState;
    if (killSwitchStableState) {
      Serial.println("[SAFETY] Physical kill switch triggered");
      wsHandler.triggerEmergencyStop("Physical kill switch");
    }
  }

  // Update all managers
  continuityManager.update();
  relayManager.update();
  wsHandler.update();
  showRunner.update();
  wifiManager.update();
  
  delay(10);
}