#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>
#include <array>

// ============================================================================
// PIN ASSIGNMENTS
// ============================================================================

// I2C (existing)
static const int I2C_SDA_PIN = 21;
static const int I2C_SCL_PIN = 22;

// MUX Select Pins (CD74HC4067 S0-S3, shared across all 3 multiplexers)
static const int MUX_S0_PIN = 16;
static const int MUX_S1_PIN = 17;
static const int MUX_S2_PIN = 18;
static const int MUX_S3_PIN = 19;

// Relay Control Pins (Active HIGH)
static const int MASTER_ARM_RELAY_PIN = 25;  // NO terminal → igniter bus power
static const int AUX_RELAY_1_PIN = 26;
static const int AUX_RELAY_2_PIN = 27;

// Safety Input Pins
static const int KILL_SWITCH_PIN = 34;  // Input-only pin, active HIGH trigger
static const uint16_t KILL_SWITCH_DEBOUNCE_MS = 50;

// ============================================================================
// I2C ADDRESSES & ADS1115 CHANNELS
// ============================================================================

// ADS1115 Analog-to-Digital Converter (0x48 = ADDR pin tied to GND)
static const uint8_t ADS1115_I2C_ADDR = 0x48;
static const uint8_t ADS1115_CHANNEL_CONTINUITY_MUX0 = 0;  // A0 → MUX0 output (zones 0–15)
static const uint8_t ADS1115_CHANNEL_CONTINUITY_MUX1 = 1;  // A1 → MUX1 output (zones 16–31)
static const uint8_t ADS1115_CHANNEL_CONTINUITY_MUX2 = 2;  // A2 → MUX2 output (zones 32–47)
static const uint8_t ADS1115_CHANNEL_BATTERY = 3;          // A3 → battery voltage divider

// PW535 Relay Board I2C Addresses
static const std::array<uint8_t, 3> BOARD_ADDRS = {0x20, 0x21, 0x22};
static const uint8_t MAX_BOARDS = 3;
static const uint8_t RELAYS_PER_BOARD = 16;
static const uint8_t MAX_ZONES = MAX_BOARDS * RELAYS_PER_BOARD;  // 48

// ============================================================================
// COMPILE-TIME DEFAULTS & SETTINGS
// ============================================================================

static const uint16_t DEFAULT_IGNITER_DURATION_MS = 2000;  // 2 seconds
static const uint8_t DEFAULT_AUTO_DELAY_SEC = 0;
static const bool DEFAULT_ABORT_ON_DISCONNECT = false;
static const uint8_t DEFAULT_BOARD_COUNT = 1;
static const uint8_t DEFAULT_ESTOP_RESET_MODE = 0;  // 0 = two-step, 1 = power cycle, 2 = any confirmation

// Battery (16S LiFePO4, voltage divider 47k + 3.3k = 15.24:1 ratio)
static const float VBAT_SCALE = 15.24f;
static const float VBAT_MAX_SINGLE_CELL = 3.65f;  // LiFePO4 cell nominal max
static const float VBAT_MIN_SINGLE_CELL = 2.5f;   // LiFePO4 cell nominal min
static const float VBAT_MAX_PACK = VBAT_MAX_SINGLE_CELL * 16.0f;  // ~58.4V
static const float VBAT_MIN_PACK = VBAT_MIN_SINGLE_CELL * 16.0f;  // ~40V

// ADS1115 Gain Setting: ±4.096V (GAIN_ONE = 0.125mV/bit resolution)
// Used for ALL channels (continuity 0–3.3V AND battery via divider)
static const uint8_t ADS1115_GAIN = 1;  // GAIN_ONE (±4.096V)

// Continuity Testing Thresholds (voltage in V)
// These are the ADC readings in volts; configurable from Settings UI
static const float DEFAULT_CONTINUITY_LOW_GOOD = 0.0001f;     // ~0V = open/no e-match
static const float DEFAULT_CONTINUITY_HI_GOOD = 0.015f;       // ~0.015V = e-match connected (good)
static const float DEFAULT_CONTINUITY_LOW_OPEN_CIRCUIT = 3.0f; // >3V = open circuit (no igniter)

// MUX Settling Time (ms)
static const uint16_t MUX_SETTLING_TIME_MS = 1;

// Continuity Scan Interval & Timing
static const uint16_t CONTINUITY_SCAN_INTERVAL_MS = 50;  // Full scan every 50ms
static const uint32_t CONTINUITY_FAST_SCAN_INTERVAL_MS = 10;  // During firing, faster reads

// WebSocket
static const uint16_t WEBSOCKET_HEARTBEAT_INTERVAL_MS = 5000;  // 5 second ping
static const uint16_t WEBSOCKET_HEARTBEAT_TIMEOUT_MS = 8000;   // 8 second heartbeat timeout
static const uint16_t WEBSOCKET_MAX_CLIENTS = 1;  // Only one controller at a time

// Show Runner (Auto Mode)
static const uint16_t SHOW_RUNNER_TICK_MS = 10;  // Update frequency

// WiFi
static const char* DEFAULT_AP_SSID = "ApexPyro";
static const char* DEFAULT_AP_PASSWORD = "apexFIRE!pyro";
static const uint16_t WIFI_CONNECT_TIMEOUT_MS = 10000;  // 10s timeout for UI-initiated client mode
static const uint16_t WIFI_BOOT_CONNECT_TIMEOUT_MS = 30000;  // 30s timeout for boot auto-reconnect
static const uint8_t WIFI_CONNECT_MAX_RETRIES = 5;
static const uint32_t WIFI_CONNECT_RETRY_BASE_MS = 2000;  // Exponential backoff base
static const uint8_t WIFI_RSSI_LEVEL_NONE = 0;
static const uint8_t WIFI_RSSI_LEVEL_LOW = -80;
static const uint8_t WIFI_RSSI_LEVEL_MEDIUM = -60;
static const uint8_t WIFI_RSSI_LEVEL_HIGH = -40;

// NVS (Non-Volatile Storage) Namespaces & Keys
namespace NVS_KEYS {
    // Namespaces
    static const char* NS_WIFI = "wifi";
    static const char* NS_ZONES = "zones";
    static const char* NS_GROUPS = "groups";
    static const char* NS_SETTINGS = "settings";
    static const char* NS_AUX = "aux";

    // WiFi keys
    static const char* WIFI_AP_SSID = "ap_ssid";
    static const char* WIFI_AP_PASS = "ap_pass";
    static const char* WIFI_CLIENT_SSID = "cl_ssid";
    static const char* WIFI_CLIENT_PASS = "cl_pass";

    // Zone keys (suffix = zone index 0–47)
    static const char* ZONE_DESC_PREFIX = "desc_";
    static const char* ZONE_TIME_PREFIX = "time_";
    static const char* ZONE_ENABLED_PREFIX = "en_";
    static const char* ZONE_GROUP_PREFIX = "grp_";
    static const char* ZONE_ORDER_PREFIX = "ord_";

    // Group keys (suffix = group index)
    static const char* GROUP_NAME_PREFIX = "grp_name_";
    static const char* GROUP_MEMBERS_PREFIX = "grp_mbrs_";
    static const char* GROUP_ORDER_PREFIX = "grp_ord_";

    // Settings keys
    static const char* SETTING_IGNITER_DURATION = "ign_dur";
    static const char* SETTING_AUTO_DELAY = "auto_delay";
    static const char* SETTING_ABORT_ON_DISCONNECT = "abort_disc";
    static const char* SETTING_ESTOP_RESET_MODE = "estop_mode";
    static const char* SETTING_BOARD_COUNT = "board_cnt";
    static const char* SETTING_CONTINUITY_LO_GOOD = "cont_lo_good";
    static const char* SETTING_CONTINUITY_HI_GOOD = "cont_hi_good";
    static const char* SETTING_CONTINUITY_LO_OPEN = "cont_lo_open";

    // Aux keys
    static const char* AUX_RELAY_1_NAME = "aux1_name";
    static const char* AUX_RELAY_2_NAME = "aux2_name";
}

// ============================================================================
// ENUMS & STRUCTURES
// ============================================================================

enum class ContinuityStatus : uint8_t {
    UNKNOWN = 0,
    GOOD = 1,
    OPEN_CIRCUIT = 2,
    SHORTED = 3,
    FIRING = 4,
};

enum class WiFiLevel : uint8_t {
    NONE_LEVEL = 0,
    LOW_LEVEL = 1,
    MEDIUM_LEVEL = 2,
    HIGH_LEVEL = 3,
};

enum class EStopResetMode : uint8_t {
    TWO_STEP_CONFIRM = 0,
    POWER_CYCLE_ONLY = 1,
    ANY_CONFIRM = 2,
};

#endif  // CONFIG_H
