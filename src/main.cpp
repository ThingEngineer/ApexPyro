#include <Arduino.h>
#include <Wire.h>

static const uint8_t BOARD_ADDRS[] = {0x20, 0x21, 0x22};
static const uint8_t BOARD_COUNT = sizeof(BOARD_ADDRS) / sizeof(BOARD_ADDRS[0]);
static const uint8_t RELAYS_PER_BOARD = 16;
static const uint8_t TOTAL_ZONES = BOARD_COUNT * RELAYS_PER_BOARD;

static const uint8_t REG_OUTPUT_PORT0 = 0x02;  // Relays A0-A7
static const uint8_t REG_OUTPUT_PORT1 = 0x03;  // Relays B0-B7
static const uint8_t REG_CONFIG_PORT0 = 0x06;  // 0 = output, 1 = input
static const uint8_t REG_CONFIG_PORT1 = 0x07;  // 0 = output, 1 = input

static const int I2C_SDA_PIN = 21;
static const int I2C_SCL_PIN = 22;

bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

void setAllRelaysOffOnBoard(uint8_t address) {
  writeRegister(address, REG_OUTPUT_PORT0, 0x00);
  writeRegister(address, REG_OUTPUT_PORT1, 0x00);
}

void setAllRelaysOff() {
  for (uint8_t i = 0; i < BOARD_COUNT; i++) {
    setAllRelaysOffOnBoard(BOARD_ADDRS[i]);
  }
}

bool initPw535(uint8_t address) {
  // CRITICAL: initialize both config ports so all 16 lines are outputs.
  bool ok0 = writeRegister(address, REG_CONFIG_PORT0, 0x00);
  bool ok1 = writeRegister(address, REG_CONFIG_PORT1, 0x00);

  // Start from a known-safe state.
  setAllRelaysOffOnBoard(address);

  return ok0 && ok1;
}

void setSingleRelayOnBoard(uint8_t address, uint8_t relayIndex) {
  // relayIndex 0-7 => port A, 8-15 => port B on a single board.
  uint8_t portA = 0x00;
  uint8_t portB = 0x00;

  if (relayIndex < 8) {
    portA = static_cast<uint8_t>(1U << relayIndex);
  } else {
    portB = static_cast<uint8_t>(1U << (relayIndex - 8));
  }

  writeRegister(address, REG_OUTPUT_PORT0, portA);
  writeRegister(address, REG_OUTPUT_PORT1, portB);
}

void setSingleZone(uint8_t zoneIndex) {
  // zoneIndex 0-31 maps across two boards.
  uint8_t boardIndex = zoneIndex / RELAYS_PER_BOARD;
  uint8_t relayIndex = zoneIndex % RELAYS_PER_BOARD;

  setAllRelaysOff();
  setSingleRelayOnBoard(BOARD_ADDRS[boardIndex], relayIndex);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Wire.setPins(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.begin();
  Wire.setClock(100000);

  Serial.println("ApexPyro 32-zone relay test starting...");
  Serial.printf("I2C: SDA=%d SCL=%d\n", I2C_SDA_PIN, I2C_SCL_PIN);

  for (uint8_t i = 0; i < BOARD_COUNT; i++) {
    uint8_t address = BOARD_ADDRS[i];
    if (!initPw535(address)) {
      Serial.printf("PW535 init failed at 0x%02X\n", address);
    } else {
      Serial.printf("PW535 initialized at 0x%02X\n", address);
    }
  }
}

void loop() {
  for (uint8_t zone = 0; zone < TOTAL_ZONES; zone++) {
    setSingleZone(zone);
    Serial.printf("Zone %u\n", zone + 1);
    delay(250);  // Zone stays ON for 0.25 seconds
    setAllRelaysOff();
    delay(250);  // 0.25 second gap before next zone
  }
}