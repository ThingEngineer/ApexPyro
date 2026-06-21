#include "storage.h"
#include <ArduinoJson.h>

// Path for zone persistence; lives in the LittleFS filesystem alongside the UI assets.
static const char* const ZONES_FILE_PATH = "/zones.json";

StorageManager storage;

StorageManager::StorageManager() : zoneCacheLoaded(false) {
    initZoneDefaults();
}

void StorageManager::begin() {
    Preferences prefs;
    const uint16_t schemaVersion = 2;

    // Seed defaults once so read paths don't repeatedly hit missing-key lookups.
    prefs.begin(NVS_KEYS::NS_WIFI, false);
    if (!prefs.isKey(NVS_KEYS::WIFI_AP_SSID)) prefs.putString(NVS_KEYS::WIFI_AP_SSID, DEFAULT_AP_SSID);
    if (!prefs.isKey(NVS_KEYS::WIFI_AP_PASS)) prefs.putString(NVS_KEYS::WIFI_AP_PASS, DEFAULT_AP_PASSWORD);
    if (!prefs.isKey(NVS_KEYS::WIFI_CLIENT_SSID)) prefs.putString(NVS_KEYS::WIFI_CLIENT_SSID, "");
    if (!prefs.isKey(NVS_KEYS::WIFI_CLIENT_PASS)) prefs.putString(NVS_KEYS::WIFI_CLIENT_PASS, "");
    prefs.end();

    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    uint16_t currentSchema = prefs.getUShort("schema_ver", 0);
    bool needsMigration = currentSchema < schemaVersion;
    if (!prefs.isKey(NVS_KEYS::SETTING_IGNITER_DURATION)) prefs.putUShort(NVS_KEYS::SETTING_IGNITER_DURATION, DEFAULT_IGNITER_DURATION_MS);
    if (!prefs.isKey(NVS_KEYS::SETTING_AUTO_DELAY)) prefs.putUChar(NVS_KEYS::SETTING_AUTO_DELAY, DEFAULT_AUTO_DELAY_SEC);
    if (!prefs.isKey(NVS_KEYS::SETTING_ABORT_ON_DISCONNECT)) prefs.putBool(NVS_KEYS::SETTING_ABORT_ON_DISCONNECT, DEFAULT_ABORT_ON_DISCONNECT);
    if (!prefs.isKey(NVS_KEYS::SETTING_ESTOP_RESET_MODE)) prefs.putUChar(NVS_KEYS::SETTING_ESTOP_RESET_MODE, DEFAULT_ESTOP_RESET_MODE);
    if (!prefs.isKey(NVS_KEYS::SETTING_BOARD_COUNT)) prefs.putUChar(NVS_KEYS::SETTING_BOARD_COUNT, DEFAULT_BOARD_COUNT);
    if (!prefs.isKey(NVS_KEYS::SETTING_CONTINUITY_LO_GOOD)) prefs.putFloat(NVS_KEYS::SETTING_CONTINUITY_LO_GOOD, DEFAULT_CONTINUITY_LOW_GOOD);
    if (!prefs.isKey(NVS_KEYS::SETTING_CONTINUITY_HI_GOOD)) prefs.putFloat(NVS_KEYS::SETTING_CONTINUITY_HI_GOOD, DEFAULT_CONTINUITY_HI_GOOD);
    if (!prefs.isKey(NVS_KEYS::SETTING_CONTINUITY_LO_OPEN)) prefs.putFloat(NVS_KEYS::SETTING_CONTINUITY_LO_OPEN, DEFAULT_CONTINUITY_LOW_OPEN_CIRCUIT);
    prefs.putUShort("schema_ver", schemaVersion);
    prefs.end();

    if (needsMigration) {
        // Reclaim NVS entries from older layouts that can exhaust space.
        prefs.begin(NVS_KEYS::NS_ZONES, false);
        prefs.clear();
        prefs.end();

        prefs.begin(NVS_KEYS::NS_GROUPS, false);
        prefs.clear();
        prefs.end();
    }

    prefs.begin(NVS_KEYS::NS_AUX, false);
    if (!prefs.isKey(NVS_KEYS::AUX_RELAY_1_NAME)) prefs.putString(NVS_KEYS::AUX_RELAY_1_NAME, "Lights");
    if (!prefs.isKey(NVS_KEYS::AUX_RELAY_2_NAME)) prefs.putString(NVS_KEYS::AUX_RELAY_2_NAME, "Music");
    prefs.end();

    // Clear now-unused NVS zone namespace to reclaim flash space.
    prefs.begin(NVS_KEYS::NS_ZONES, false);
    prefs.clear();
    prefs.end();

    // Legacy namespace from removed standalone group editor.
    prefs.begin(NVS_KEYS::NS_GROUPS, false);
    prefs.clear();
    prefs.end();

    // Mount LittleFS (idempotent if already mounted by websocket_handler).
    if (!LittleFS.begin()) {
        Serial.println("[Storage] LittleFS mount failed; zone data will be in-memory only");
    } else {
        loadZonesFromFile();
    }
}

void StorageManager::loadAll() {
    if (!zoneCacheLoaded) {
        loadZonesFromFile();
    }
}

void StorageManager::initZoneDefaults() {
    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        zoneCache[i].description = "";
        zoneCache[i].time        = 0.0f;
        zoneCache[i].enabled     = false;
        zoneCache[i].group       = 0;
        zoneCache[i].order       = i;
    }
}

bool StorageManager::loadZonesFromFile() {
    File f = LittleFS.open(ZONES_FILE_PATH, "r");
    if (!f) {
        Serial.println("[Storage] zones.json not found; using defaults");
        zoneCacheLoaded = true;
        return false;
    }

    size_t size = f.size();
    if (size == 0 || size > 8192) {
        f.close();
        Serial.printf("[Storage] zones.json unexpected size %u; using defaults\n", size);
        zoneCacheLoaded = true;
        return false;
    }

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err || !doc.is<JsonArray>()) {
        Serial.printf("[Storage] zones.json parse error: %s\n", err.c_str());
        zoneCacheLoaded = true;
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject zone : arr) {
        uint8_t idx = zone["i"] | 255;
        if (idx >= MAX_ZONES) continue;
        zoneCache[idx].description = zone["d"].as<String>();
        zoneCache[idx].time        = zone["t"] | 0.0f;
        zoneCache[idx].enabled     = zone["e"] | false;
        zoneCache[idx].group       = zone["g"] | (uint8_t)0;
        zoneCache[idx].order       = zone["o"] | idx;
    }

    zoneCacheLoaded = true;
    Serial.println("[Storage] zones.json loaded");
    return true;
}

bool StorageManager::saveZonesToFile() {
    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();

    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        const ZoneData& z = zoneCache[i];
        // Only write non-default zones to save space; always write if group/order/desc set.
        if (z.description.length() == 0 && z.time == 0.0f && z.group == 0 &&
            z.order == i && !z.enabled) {
            continue;
        }
        JsonObject entry = arr.createNestedObject();
        entry["i"] = i;
        entry["d"] = z.description;
        entry["t"] = z.time;
        entry["e"] = z.enabled;
        entry["g"] = z.group;
        entry["o"] = z.order;
    }

    File f = LittleFS.open(ZONES_FILE_PATH, "w");
    if (!f) {
        Serial.println("[Storage] Failed to open zones.json for write");
        return false;
    }
    serializeJson(doc, f);
    f.close();
    return true;
}

String StorageManager::makeZoneKey(const char* prefix, uint8_t zoneIdx) {
    String key = String(prefix);
    if (zoneIdx < 10) key += "0";
    key += String(zoneIdx);
    return key;
}

// ============================================================================
// WiFi
// ============================================================================

String StorageManager::getApSSID() {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_WIFI, true);
    String ssid = prefs.getString(NVS_KEYS::WIFI_AP_SSID, DEFAULT_AP_SSID);
    prefs.end();
    return ssid;
}

String StorageManager::getApPassword() {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_WIFI, true);
    String pass = prefs.getString(NVS_KEYS::WIFI_AP_PASS, DEFAULT_AP_PASSWORD);
    prefs.end();
    return pass;
}

String StorageManager::getClientSSID() {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_WIFI, true);
    String ssid = prefs.getString(NVS_KEYS::WIFI_CLIENT_SSID, "");
    prefs.end();
    return ssid;
}

String StorageManager::getClientPassword() {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_WIFI, true);
    String pass = prefs.getString(NVS_KEYS::WIFI_CLIENT_PASS, "");
    prefs.end();
    return pass;
}

void StorageManager::setApCredentials(const String& ssid, const String& pass) {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_WIFI, false);
    prefs.putString(NVS_KEYS::WIFI_AP_SSID, ssid);
    prefs.putString(NVS_KEYS::WIFI_AP_PASS, pass);
    prefs.end();
}

void StorageManager::setClientCredentials(const String& ssid, const String& pass) {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_WIFI, false);
    prefs.putString(NVS_KEYS::WIFI_CLIENT_SSID, ssid);
    prefs.putString(NVS_KEYS::WIFI_CLIENT_PASS, pass);
    prefs.end();
}

// ============================================================================
// Zones  — backed by in-memory cache + LittleFS; no NVS usage
// ============================================================================

String StorageManager::getZoneDescription(uint8_t zoneIdx) {
    if (zoneIdx >= MAX_ZONES) return "";
    return zoneCache[zoneIdx].description;
}

float StorageManager::getZoneTime(uint8_t zoneIdx) {
    if (zoneIdx >= MAX_ZONES) return 0.0f;
    return zoneCache[zoneIdx].time;
}

bool StorageManager::isZoneEnabled(uint8_t zoneIdx) {
    if (zoneIdx >= MAX_ZONES) return false;
    return zoneCache[zoneIdx].enabled;
}

uint8_t StorageManager::getZoneGroup(uint8_t zoneIdx) {
    if (zoneIdx >= MAX_ZONES) return 0;
    return zoneCache[zoneIdx].group;
}

uint8_t StorageManager::getZoneOrder(uint8_t zoneIdx) {
    if (zoneIdx >= MAX_ZONES) return zoneIdx;
    return zoneCache[zoneIdx].order;
}

void StorageManager::setZoneDescription(uint8_t zoneIdx, const String& desc) {
    if (zoneIdx >= MAX_ZONES) return;
    zoneCache[zoneIdx].description = desc;
    saveZonesToFile();
}

void StorageManager::setZoneTime(uint8_t zoneIdx, float timeSeconds) {
    if (zoneIdx >= MAX_ZONES) return;
    zoneCache[zoneIdx].time = timeSeconds;
    saveZonesToFile();
}

void StorageManager::setZoneEnabled(uint8_t zoneIdx, bool enabled) {
    if (zoneIdx >= MAX_ZONES) return;
    zoneCache[zoneIdx].enabled = enabled;
    saveZonesToFile();
}

void StorageManager::setZoneGroup(uint8_t zoneIdx, uint8_t groupIdx) {
    if (zoneIdx >= MAX_ZONES) return;
    zoneCache[zoneIdx].group = groupIdx;
    saveZonesToFile();
}

void StorageManager::setZoneOrder(uint8_t zoneIdx, uint8_t order) {
    if (zoneIdx >= MAX_ZONES) return;
    zoneCache[zoneIdx].order = order;
    saveZonesToFile();
}

void StorageManager::setZoneBatch(uint8_t zoneIdx, const String& desc, float time,
                                   bool enabled, uint8_t group, uint8_t order) {
    if (zoneIdx >= MAX_ZONES) return;
    zoneCache[zoneIdx].description = desc;
    zoneCache[zoneIdx].time        = time;
    zoneCache[zoneIdx].enabled     = enabled;
    zoneCache[zoneIdx].group       = group;
    zoneCache[zoneIdx].order       = order;
    saveZonesToFile();
}

void StorageManager::saveZone(uint8_t zoneIdx) {
    (void)zoneIdx;  // all saves go through setZoneBatch or individual setters above
}

// ============================================================================
// Settings
// ============================================================================

uint16_t StorageManager::getIgniterDuration() {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    uint16_t dur = prefs.getUShort(NVS_KEYS::SETTING_IGNITER_DURATION, DEFAULT_IGNITER_DURATION_MS);
    prefs.end();
    return dur;
}

uint8_t StorageManager::getAutoDelay() {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    uint8_t delay = prefs.getUChar(NVS_KEYS::SETTING_AUTO_DELAY, DEFAULT_AUTO_DELAY_SEC);
    prefs.end();
    return delay;
}

bool StorageManager::getAbortOnDisconnect() {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    bool flag = prefs.getBool(NVS_KEYS::SETTING_ABORT_ON_DISCONNECT, DEFAULT_ABORT_ON_DISCONNECT);
    prefs.end();
    return flag;
}

EStopResetMode StorageManager::getEStopResetMode() {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    uint8_t mode = prefs.getUChar(NVS_KEYS::SETTING_ESTOP_RESET_MODE, DEFAULT_ESTOP_RESET_MODE);
    prefs.end();
    return static_cast<EStopResetMode>(mode);
}

uint8_t StorageManager::getBoardCount() {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    uint8_t cnt = prefs.getUChar(NVS_KEYS::SETTING_BOARD_COUNT, DEFAULT_BOARD_COUNT);
    prefs.end();
    return cnt;
}

float StorageManager::getContinuityLoGood() {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    float val = prefs.getFloat(NVS_KEYS::SETTING_CONTINUITY_LO_GOOD, DEFAULT_CONTINUITY_LOW_GOOD);
    prefs.end();
    return val;
}

float StorageManager::getContinuityHiGood() {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    float val = prefs.getFloat(NVS_KEYS::SETTING_CONTINUITY_HI_GOOD, DEFAULT_CONTINUITY_HI_GOOD);
    prefs.end();
    return val;
}

float StorageManager::getContinuityLoOpen() {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    float val = prefs.getFloat(NVS_KEYS::SETTING_CONTINUITY_LO_OPEN, DEFAULT_CONTINUITY_LOW_OPEN_CIRCUIT);
    prefs.end();
    return val;
}

void StorageManager::setIgniterDuration(uint16_t ms) {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putUShort(NVS_KEYS::SETTING_IGNITER_DURATION, ms);
    prefs.end();
}

void StorageManager::setAutoDelay(uint8_t sec) {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putUChar(NVS_KEYS::SETTING_AUTO_DELAY, sec);
    prefs.end();
}

void StorageManager::setAbortOnDisconnect(bool flag) {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putBool(NVS_KEYS::SETTING_ABORT_ON_DISCONNECT, flag);
    prefs.end();
}

void StorageManager::setEStopResetMode(EStopResetMode mode) {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putUChar(NVS_KEYS::SETTING_ESTOP_RESET_MODE, static_cast<uint8_t>(mode));
    prefs.end();
}

void StorageManager::setBoardCount(uint8_t count) {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putUChar(NVS_KEYS::SETTING_BOARD_COUNT, count);
    prefs.end();
}

void StorageManager::setContinuityThresholds(float loGood, float hiGood, float loOpen) {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putFloat(NVS_KEYS::SETTING_CONTINUITY_LO_GOOD, loGood);
    prefs.putFloat(NVS_KEYS::SETTING_CONTINUITY_HI_GOOD, hiGood);
    prefs.putFloat(NVS_KEYS::SETTING_CONTINUITY_LO_OPEN, loOpen);
    prefs.end();
}

// ============================================================================
// Aux Relays
// ============================================================================

String StorageManager::getAuxRelayName(uint8_t relayIdx) {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_AUX, true);
    const char* key = (relayIdx == 0) ? NVS_KEYS::AUX_RELAY_1_NAME : NVS_KEYS::AUX_RELAY_2_NAME;
    String name = prefs.getString(key, (relayIdx == 0) ? "Lights" : "Music");
    prefs.end();
    return name;
}

void StorageManager::setAuxRelayName(uint8_t relayIdx, const String& name) {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_AUX, false);
    const char* key = (relayIdx == 0) ? NVS_KEYS::AUX_RELAY_1_NAME : NVS_KEYS::AUX_RELAY_2_NAME;
    prefs.putString(key, name);
    prefs.end();
}

// ============================================================================
// Import/Export
// ============================================================================

String StorageManager::exportShowJson() {
    DynamicJsonDocument doc(8192);

    JsonArray zonesArray = doc.createNestedArray("zones");
    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        const ZoneData& z = zoneCache[i];
        if (z.description.length() == 0 && z.time == 0.0f && z.group == 0 &&
            z.order == i && !z.enabled) {
            continue;  // skip unmodified default zones to keep export compact
        }
        JsonObject zone = zonesArray.createNestedObject();
        zone["index"]       = i;
        zone["description"] = z.description;
        zone["time"]        = z.time;
        zone["enabled"]     = z.enabled;
        zone["group"]       = z.group;
        zone["order"]       = z.order;
    }

    String jsonStr;
    serializeJson(doc, jsonStr);
    return jsonStr;
}

bool StorageManager::importShowJson(const String& jsonStr) {
    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, jsonStr);

    if (error) {
        Serial.printf("Import JSON parse error: %s\n", error.c_str());
        return false;
    }

    if (!doc.containsKey("zones") || !doc["zones"].is<JsonArray>()) {
        Serial.println("[Storage] Import show payload missing zones array");
        return false;
    }

    // Replace semantics: start from firmware defaults and then apply imported entries.
    initZoneDefaults();

    JsonArray zonesArray = doc["zones"].as<JsonArray>();
    bool appliedAnyZone = false;
    for (JsonObject zone : zonesArray) {
        if (!zone.containsKey("index")) {
            continue;
        }

        int idx = zone["index"].as<int>();
        if (idx < 0 || idx >= MAX_ZONES) {
            continue;
        }

        ZoneData updated = zoneCache[idx];
        if (zone.containsKey("description")) {
            updated.description = zone["description"].as<String>();
        }
        if (zone.containsKey("time")) {
            updated.time = zone["time"].as<float>();
        }
        if (zone.containsKey("enabled")) {
            updated.enabled = zone["enabled"].as<bool>();
        }
        if (zone.containsKey("group")) {
            updated.group = static_cast<uint8_t>(constrain(zone["group"].as<int>(), 0, 15));
        }
        if (zone.containsKey("order")) {
            updated.order = static_cast<uint8_t>(constrain(zone["order"].as<int>(), 0, MAX_ZONES - 1));
        }

        zoneCache[idx] = updated;
        appliedAnyZone = true;
    }

    if (!appliedAnyZone) {
        Serial.println("[Storage] Import show payload contained no valid zone entries");
        return false;
    }

    if (!saveZonesToFile()) {
        Serial.println("[Storage] Failed to save imported show payload");
        return false;
    }

    return true;
}

String StorageManager::exportSettingsJson() {
    DynamicJsonDocument doc(2048);

    JsonObject settings = doc.createNestedObject("settings");
    settings["igniterDurationMs"] = getIgniterDuration();
    settings["autoDelay"] = getAutoDelay();
    settings["abortOnDisconnect"] = getAbortOnDisconnect();
    settings["eStopResetMode"] = static_cast<uint8_t>(getEStopResetMode());
    settings["continuityLoGood"] = getContinuityLoGood();
    settings["continuityHiGood"] = getContinuityHiGood();
    settings["continuityLoOpen"] = getContinuityLoOpen();

    JsonArray auxNames = doc.createNestedArray("auxNames");
    auxNames.add(getAuxRelayName(0));
    auxNames.add(getAuxRelayName(1));

    String jsonStr;
    serializeJson(doc, jsonStr);
    return jsonStr;
}

bool StorageManager::importSettingsJson(const String& jsonStr) {
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, jsonStr);
    if (error) {
        Serial.printf("[Storage] Settings import parse error: %s\n", error.c_str());
        return false;
    }

    bool appliedAnySetting = false;
    if (doc.containsKey("settings") && doc["settings"].is<JsonObject>()) {
        JsonObject settings = doc["settings"].as<JsonObject>();

        if (settings.containsKey("igniterDurationMs")) {
            int durationMs = constrain(settings["igniterDurationMs"].as<int>(), 100, 10000);
            setIgniterDuration(static_cast<uint16_t>(durationMs));
            appliedAnySetting = true;
        }
        if (settings.containsKey("autoDelay")) {
            int autoDelay = constrain(settings["autoDelay"].as<int>(), 0, 60);
            setAutoDelay(static_cast<uint8_t>(autoDelay));
            appliedAnySetting = true;
        }
        if (settings.containsKey("abortOnDisconnect")) {
            setAbortOnDisconnect(settings["abortOnDisconnect"].as<bool>());
            appliedAnySetting = true;
        }
        if (settings.containsKey("eStopResetMode")) {
            int mode = constrain(settings["eStopResetMode"].as<int>(), 0, 2);
            setEStopResetMode(static_cast<EStopResetMode>(mode));
            appliedAnySetting = true;
        }

        float loGood = getContinuityLoGood();
        float hiGood = getContinuityHiGood();
        float loOpen = getContinuityLoOpen();
        bool continuityUpdated = false;

        if (settings.containsKey("continuityLoGood")) {
            loGood = constrain(settings["continuityLoGood"].as<float>(), 0.0f, 5.0f);
            continuityUpdated = true;
        }
        if (settings.containsKey("continuityHiGood")) {
            hiGood = constrain(settings["continuityHiGood"].as<float>(), 0.0f, 5.0f);
            continuityUpdated = true;
        }
        if (settings.containsKey("continuityLoOpen")) {
            loOpen = constrain(settings["continuityLoOpen"].as<float>(), 0.0f, 5.0f);
            continuityUpdated = true;
        }

        if (continuityUpdated) {
            setContinuityThresholds(loGood, hiGood, loOpen);
            appliedAnySetting = true;
        }
    }

    if (doc.containsKey("auxNames") && doc["auxNames"].is<JsonArray>()) {
        JsonArray auxNames = doc["auxNames"].as<JsonArray>();
        if (auxNames.size() >= 2) {
            setAuxRelayName(0, auxNames[0].as<String>());
            setAuxRelayName(1, auxNames[1].as<String>());
            appliedAnySetting = true;
        }
    }

    if (!appliedAnySetting) {
        Serial.println("[Storage] Settings import payload contained no supported values");
        return false;
    }

    return true;
}

// ============================================================================
// Utilities
// ============================================================================

void StorageManager::clearAllZones() {
    initZoneDefaults();
    LittleFS.remove(ZONES_FILE_PATH);
}

void StorageManager::resetToDefaults() {
    Preferences prefs;
    // Clear all NVS namespaces.
    prefs.begin(NVS_KEYS::NS_WIFI, false);     prefs.clear(); prefs.end();
    prefs.begin(NVS_KEYS::NS_SETTINGS, false); prefs.clear(); prefs.end();
    prefs.begin(NVS_KEYS::NS_AUX, false);      prefs.clear(); prefs.end();

    // Remove LittleFS zone file and reset cache.
    LittleFS.remove(ZONES_FILE_PATH);
    initZoneDefaults();

    begin();
}

void StorageManager::saveSetting(const char* key, const String& value) {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putString(key, value);
    prefs.end();
}

void StorageManager::saveSetting(const char* key, int value) {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putInt(key, value);
    prefs.end();
}

void StorageManager::saveSetting(const char* key, bool value) {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putBool(key, value);
    prefs.end();
}

void StorageManager::saveSetting(const char* key, float value) {
    Preferences prefs;
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putFloat(key, value);
    prefs.end();
}
