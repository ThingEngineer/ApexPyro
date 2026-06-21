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
static const int AUX_RELAY_3_PIN = 32;
static const uint8_t AUX_RELAY_COUNT = 3;

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

static const uint8_t BATTERY_MAX_CURVE_POINTS = 8;

enum class BatteryProfile : uint8_t {
    LIFEPO4 = 0,
    LI_ION = 1,
    LIPO = 2,
    LEAD_ACID_FLOODED = 3,
    AGM = 4,
    GEL_LEAD_ACID = 5,
    LTO = 6,
    NIMH = 7,
    CUSTOM = 8,
};

struct BatteryCurvePoint {
    float voltage;
    uint8_t soc;
};

struct BatteryProfilePreset {
    BatteryProfile profile;
    const char* label;
    float cellMin;
    float cellNominal;
    float cellMax;
    uint8_t defaultCellCount;
    float lowWarnCell;
    uint8_t pointCount;
    std::array<BatteryCurvePoint, BATTERY_MAX_CURVE_POINTS> points;
    const char* summary;
};

struct BatteryCustomConfig {
    uint8_t cellCount;
    float packMin;
    float packMax;
    float lowWarn;
    uint16_t sampleIntervalMs;
    uint8_t pointCount;
    std::array<BatteryCurvePoint, BATTERY_MAX_CURVE_POINTS> points;
};

static const uint8_t DEFAULT_BATTERY_PROFILE = static_cast<uint8_t>(BatteryProfile::LIFEPO4);
static const uint8_t DEFAULT_BATTERY_CUSTOM_CELL_COUNT = 16;
static const float DEFAULT_BATTERY_CUSTOM_PACK_MIN = 40.0f;
static const float DEFAULT_BATTERY_CUSTOM_PACK_MAX = 58.4f;
static const float DEFAULT_BATTERY_CUSTOM_LOW_WARN = 46.0f;
static const uint16_t DEFAULT_BATTERY_SAMPLE_INTERVAL_MS = 5000;

static const uint8_t DEFAULT_BATTERY_CUSTOM_POINT_COUNT = 6;
static const std::array<BatteryCurvePoint, BATTERY_MAX_CURVE_POINTS> DEFAULT_BATTERY_CUSTOM_POINTS = {{
    {40.0f, 0},
    {48.0f, 20},
    {51.2f, 50},
    {54.4f, 75},
    {56.8f, 90},
    {58.4f, 100},
    {0.0f, 0},
    {0.0f, 0},
}};

static const std::array<BatteryProfilePreset, 8> BATTERY_PROFILE_PRESETS = {{
    {
        BatteryProfile::LIFEPO4,
        "LiFePO4 Lithium",
        2.50f, 3.20f, 3.65f,
        16,
        2.90f,
        8,
        {{{2.50f, 0}, {3.00f, 10}, {3.20f, 30}, {3.27f, 50}, {3.30f, 70}, {3.35f, 85}, {3.40f, 95}, {3.60f, 100}}},
        "Flat discharge curve with steep drop near empty"
    },
    {
        BatteryProfile::LI_ION,
        "Lithium-ion (NMC/NCA)",
        3.00f, 3.70f, 4.20f,
        14,
        3.40f,
        8,
        {{{3.00f, 0}, {3.30f, 10}, {3.50f, 25}, {3.65f, 40}, {3.75f, 55}, {3.85f, 70}, {4.00f, 85}, {4.20f, 100}}},
        "Higher top-end voltage with gradual mid-pack decline"
    },
    {
        BatteryProfile::LIPO,
        "Lithium Polymer (LiPo)",
        3.20f, 3.70f, 4.20f,
        14,
        3.50f,
        8,
        {{{3.20f, 0}, {3.40f, 15}, {3.55f, 30}, {3.70f, 50}, {3.80f, 65}, {3.90f, 80}, {4.05f, 92}, {4.20f, 100}}},
        "Fast response chemistry; avoid deep discharge"
    },
    {
        BatteryProfile::LEAD_ACID_FLOODED,
        "Lead-Acid Flooded",
        1.75f, 2.05f, 2.15f,
        24,
        1.90f,
        8,
        {{{1.75f, 0}, {1.90f, 15}, {1.97f, 30}, {2.01f, 45}, {2.05f, 60}, {2.09f, 75}, {2.12f, 90}, {2.15f, 100}}},
        "Voltage sag under load; use conservative low threshold"
    },
    {
        BatteryProfile::AGM,
        "AGM",
        1.80f, 2.05f, 2.15f,
        24,
        1.92f,
        8,
        {{{1.80f, 0}, {1.93f, 15}, {1.99f, 30}, {2.03f, 45}, {2.06f, 60}, {2.10f, 75}, {2.13f, 90}, {2.15f, 100}}},
        "Lower internal resistance than flooded lead-acid"
    },
    {
        BatteryProfile::GEL_LEAD_ACID,
        "Gel Lead-Acid",
        1.75f, 2.03f, 2.14f,
        24,
        1.90f,
        8,
        {{{1.75f, 0}, {1.90f, 15}, {1.96f, 30}, {2.00f, 45}, {2.04f, 60}, {2.08f, 75}, {2.11f, 90}, {2.14f, 100}}},
        "Gentle charge/discharge profile with lower charge acceptance"
    },
    {
        BatteryProfile::LTO,
        "LTO (Lithium Titanate)",
        1.80f, 2.30f, 2.80f,
        24,
        2.00f,
        8,
        {{{1.80f, 0}, {2.00f, 15}, {2.15f, 30}, {2.25f, 45}, {2.35f, 60}, {2.45f, 75}, {2.60f, 90}, {2.80f, 100}}},
        "Wide cycle-life chemistry with lower nominal voltage"
    },
    {
        BatteryProfile::NIMH,
        "NiMH",
        1.00f, 1.20f, 1.45f,
        40,
        1.10f,
        8,
        {{{1.00f, 0}, {1.08f, 15}, {1.14f, 30}, {1.19f, 45}, {1.23f, 60}, {1.27f, 75}, {1.34f, 90}, {1.45f, 100}}},
        "Voltage varies by temperature and load near full charge"
    },
}};

inline const BatteryProfilePreset* getBatteryProfilePreset(BatteryProfile profile) {
    for (size_t i = 0; i < BATTERY_PROFILE_PRESETS.size(); i++) {
        if (BATTERY_PROFILE_PRESETS[i].profile == profile) {
            return &BATTERY_PROFILE_PRESETS[i];
        }
    }
    return &BATTERY_PROFILE_PRESETS[0];
}

inline const char* getBatteryProfileLabel(BatteryProfile profile) {
    if (profile == BatteryProfile::CUSTOM) {
        return "Custom";
    }
    return getBatteryProfilePreset(profile)->label;
}

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
    static const char* SETTING_BATTERY_PROFILE = "bat_prof";
    static const char* SETTING_BATTERY_CELL_COUNT = "bat_cells";
    static const char* SETTING_BATTERY_PACK_MIN = "bat_min";
    static const char* SETTING_BATTERY_PACK_MAX = "bat_max";
    static const char* SETTING_BATTERY_LOW_WARN = "bat_warn";
    static const char* SETTING_BATTERY_SAMPLE_INTERVAL = "bat_sm_ms";
    static const char* SETTING_BATTERY_CURVE_POINT_COUNT = "bat_ptc";
    static const char* SETTING_BATTERY_POINT0_V = "bat_p0v";
    static const char* SETTING_BATTERY_POINT0_S = "bat_p0s";
    static const char* SETTING_BATTERY_POINT1_V = "bat_p1v";
    static const char* SETTING_BATTERY_POINT1_S = "bat_p1s";
    static const char* SETTING_BATTERY_POINT2_V = "bat_p2v";
    static const char* SETTING_BATTERY_POINT2_S = "bat_p2s";
    static const char* SETTING_BATTERY_POINT3_V = "bat_p3v";
    static const char* SETTING_BATTERY_POINT3_S = "bat_p3s";
    static const char* SETTING_BATTERY_POINT4_V = "bat_p4v";
    static const char* SETTING_BATTERY_POINT4_S = "bat_p4s";
    static const char* SETTING_BATTERY_POINT5_V = "bat_p5v";
    static const char* SETTING_BATTERY_POINT5_S = "bat_p5s";
    static const char* SETTING_BATTERY_POINT6_V = "bat_p6v";
    static const char* SETTING_BATTERY_POINT6_S = "bat_p6s";
    static const char* SETTING_BATTERY_POINT7_V = "bat_p7v";
    static const char* SETTING_BATTERY_POINT7_S = "bat_p7s";

    // Aux keys
    static const char* AUX_RELAY_1_NAME = "aux1_name";
    static const char* AUX_RELAY_2_NAME = "aux2_name";
    static const char* AUX_RELAY_3_NAME = "aux3_name";
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
