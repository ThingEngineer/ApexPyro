#include "storage.h"
#include <ArduinoJson.h>

StorageManager storage;

StorageManager::StorageManager() {
}

void StorageManager::begin() {
    // Preferences will auto-initialize on first use
}

void StorageManager::loadAll() {
    // This is mainly a placeholder; getters are lazy-loaded from NVS on demand
}

String StorageManager::makeZoneKey(const char* prefix, uint8_t zoneIdx) {
    String key = String(prefix);
    if (zoneIdx < 10) key += "0";
    key += String(zoneIdx);
    return key;
}

String StorageManager::makeGroupKey(const char* prefix, uint8_t groupIdx) {
    String key = String(prefix);
    if (groupIdx < 10) key += "0";
    key += String(groupIdx);
    return key;
}

// ============================================================================
// WiFi
// ============================================================================

String StorageManager::getApSSID() {
    prefs.begin(NVS_KEYS::NS_WIFI, true);
    String ssid = prefs.getString(NVS_KEYS::WIFI_AP_SSID, DEFAULT_AP_SSID);
    prefs.end();
    return ssid;
}

String StorageManager::getApPassword() {
    prefs.begin(NVS_KEYS::NS_WIFI, true);
    String pass = prefs.getString(NVS_KEYS::WIFI_AP_PASS, DEFAULT_AP_PASSWORD);
    prefs.end();
    return pass;
}

String StorageManager::getClientSSID() {
    prefs.begin(NVS_KEYS::NS_WIFI, true);
    String ssid = prefs.getString(NVS_KEYS::WIFI_CLIENT_SSID, "");
    prefs.end();
    return ssid;
}

String StorageManager::getClientPassword() {
    prefs.begin(NVS_KEYS::NS_WIFI, true);
    String pass = prefs.getString(NVS_KEYS::WIFI_CLIENT_PASS, "");
    prefs.end();
    return pass;
}

void StorageManager::setApCredentials(const String& ssid, const String& pass) {
    prefs.begin(NVS_KEYS::NS_WIFI, false);
    prefs.putString(NVS_KEYS::WIFI_AP_SSID, ssid);
    prefs.putString(NVS_KEYS::WIFI_AP_PASS, pass);
    prefs.end();
}

void StorageManager::setClientCredentials(const String& ssid, const String& pass) {
    prefs.begin(NVS_KEYS::NS_WIFI, false);
    prefs.putString(NVS_KEYS::WIFI_CLIENT_SSID, ssid);
    prefs.putString(NVS_KEYS::WIFI_CLIENT_PASS, pass);
    prefs.end();
}

// ============================================================================
// Zones
// ============================================================================

String StorageManager::getZoneDescription(uint8_t zoneIdx) {
    prefs.begin(NVS_KEYS::NS_ZONES, true);
    String key = makeZoneKey(NVS_KEYS::ZONE_DESC_PREFIX, zoneIdx);
    String desc = prefs.getString(key.c_str(), "");
    prefs.end();
    return desc;
}

float StorageManager::getZoneTime(uint8_t zoneIdx) {
    prefs.begin(NVS_KEYS::NS_ZONES, true);
    String key = makeZoneKey(NVS_KEYS::ZONE_TIME_PREFIX, zoneIdx);
    float time = prefs.getFloat(key.c_str(), 0.0f);
    prefs.end();
    return time;
}

bool StorageManager::isZoneEnabled(uint8_t zoneIdx) {
    prefs.begin(NVS_KEYS::NS_ZONES, true);
    String key = makeZoneKey(NVS_KEYS::ZONE_ENABLED_PREFIX, zoneIdx);
    bool enabled = prefs.getBool(key.c_str(), true);
    prefs.end();
    return enabled;
}

uint8_t StorageManager::getZoneGroup(uint8_t zoneIdx) {
    prefs.begin(NVS_KEYS::NS_ZONES, true);
    String key = makeZoneKey(NVS_KEYS::ZONE_GROUP_PREFIX, zoneIdx);
    uint8_t grp = prefs.getUChar(key.c_str(), 0);
    prefs.end();
    return grp;
}

uint8_t StorageManager::getZoneOrder(uint8_t zoneIdx) {
    prefs.begin(NVS_KEYS::NS_ZONES, true);
    String key = makeZoneKey(NVS_KEYS::ZONE_ORDER_PREFIX, zoneIdx);
    uint8_t ord = prefs.getUChar(key.c_str(), zoneIdx);
    prefs.end();
    return ord;
}

void StorageManager::setZoneDescription(uint8_t zoneIdx, const String& desc) {
    prefs.begin(NVS_KEYS::NS_ZONES, false);
    String key = makeZoneKey(NVS_KEYS::ZONE_DESC_PREFIX, zoneIdx);
    prefs.putString(key.c_str(), desc);
    prefs.end();
}

void StorageManager::setZoneTime(uint8_t zoneIdx, float timeSeconds) {
    prefs.begin(NVS_KEYS::NS_ZONES, false);
    String key = makeZoneKey(NVS_KEYS::ZONE_TIME_PREFIX, zoneIdx);
    prefs.putFloat(key.c_str(), timeSeconds);
    prefs.end();
}

void StorageManager::setZoneEnabled(uint8_t zoneIdx, bool enabled) {
    prefs.begin(NVS_KEYS::NS_ZONES, false);
    String key = makeZoneKey(NVS_KEYS::ZONE_ENABLED_PREFIX, zoneIdx);
    prefs.putBool(key.c_str(), enabled);
    prefs.end();
}

void StorageManager::setZoneGroup(uint8_t zoneIdx, uint8_t groupIdx) {
    prefs.begin(NVS_KEYS::NS_ZONES, false);
    String key = makeZoneKey(NVS_KEYS::ZONE_GROUP_PREFIX, zoneIdx);
    prefs.putUChar(key.c_str(), groupIdx);
    prefs.end();
}

void StorageManager::setZoneOrder(uint8_t zoneIdx, uint8_t order) {
    prefs.begin(NVS_KEYS::NS_ZONES, false);
    String key = makeZoneKey(NVS_KEYS::ZONE_ORDER_PREFIX, zoneIdx);
    prefs.putUChar(key.c_str(), order);
    prefs.end();
}

void StorageManager::saveZone(uint8_t zoneIdx) {
    // Already saved via individual setters; this is a no-op or batch save if needed
}

// ============================================================================
// Groups
// ============================================================================

String StorageManager::getGroupName(uint8_t groupIdx) {
    prefs.begin(NVS_KEYS::NS_GROUPS, true);
    String key = makeGroupKey(NVS_KEYS::GROUP_NAME_PREFIX, groupIdx);
    String name = prefs.getString(key.c_str(), "");
    prefs.end();
    return name;
}

String StorageManager::getGroupMembers(uint8_t groupIdx) {
    prefs.begin(NVS_KEYS::NS_GROUPS, true);
    String key = makeGroupKey(NVS_KEYS::GROUP_MEMBERS_PREFIX, groupIdx);
    String members = prefs.getString(key.c_str(), "");
    prefs.end();
    return members;
}

uint8_t StorageManager::getGroupOrder(uint8_t groupIdx) {
    prefs.begin(NVS_KEYS::NS_GROUPS, true);
    String key = makeGroupKey(NVS_KEYS::GROUP_ORDER_PREFIX, groupIdx);
    uint8_t ord = prefs.getUChar(key.c_str(), groupIdx);
    prefs.end();
    return ord;
}

void StorageManager::setGroupName(uint8_t groupIdx, const String& name) {
    prefs.begin(NVS_KEYS::NS_GROUPS, false);
    String key = makeGroupKey(NVS_KEYS::GROUP_NAME_PREFIX, groupIdx);
    prefs.putString(key.c_str(), name);
    prefs.end();
}

void StorageManager::setGroupMembers(uint8_t groupIdx, const String& members) {
    prefs.begin(NVS_KEYS::NS_GROUPS, false);
    String key = makeGroupKey(NVS_KEYS::GROUP_MEMBERS_PREFIX, groupIdx);
    prefs.putString(key.c_str(), members);
    prefs.end();
}

void StorageManager::setGroupOrder(uint8_t groupIdx, uint8_t order) {
    prefs.begin(NVS_KEYS::NS_GROUPS, false);
    String key = makeGroupKey(NVS_KEYS::GROUP_ORDER_PREFIX, groupIdx);
    prefs.putUChar(key.c_str(), order);
    prefs.end();
}

// ============================================================================
// Settings
// ============================================================================

uint16_t StorageManager::getIgniterDuration() {
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    uint16_t dur = prefs.getUShort(NVS_KEYS::SETTING_IGNITER_DURATION, DEFAULT_IGNITER_DURATION_MS);
    prefs.end();
    return dur;
}

uint8_t StorageManager::getAutoDelay() {
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    uint8_t delay = prefs.getUChar(NVS_KEYS::SETTING_AUTO_DELAY, DEFAULT_AUTO_DELAY_SEC);
    prefs.end();
    return delay;
}

bool StorageManager::getAbortOnDisconnect() {
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    bool flag = prefs.getBool(NVS_KEYS::SETTING_ABORT_ON_DISCONNECT, DEFAULT_ABORT_ON_DISCONNECT);
    prefs.end();
    return flag;
}

EStopResetMode StorageManager::getEStopResetMode() {
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    uint8_t mode = prefs.getUChar(NVS_KEYS::SETTING_ESTOP_RESET_MODE, DEFAULT_ESTOP_RESET_MODE);
    prefs.end();
    return static_cast<EStopResetMode>(mode);
}

uint8_t StorageManager::getBoardCount() {
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    uint8_t cnt = prefs.getUChar(NVS_KEYS::SETTING_BOARD_COUNT, DEFAULT_BOARD_COUNT);
    prefs.end();
    return cnt;
}

float StorageManager::getContinuityLoGood() {
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    float val = prefs.getFloat(NVS_KEYS::SETTING_CONTINUITY_LO_GOOD, DEFAULT_CONTINUITY_LOW_GOOD);
    prefs.end();
    return val;
}

float StorageManager::getContinuityHiGood() {
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    float val = prefs.getFloat(NVS_KEYS::SETTING_CONTINUITY_HI_GOOD, DEFAULT_CONTINUITY_HI_GOOD);
    prefs.end();
    return val;
}

float StorageManager::getContinuityLoOpen() {
    prefs.begin(NVS_KEYS::NS_SETTINGS, true);
    float val = prefs.getFloat(NVS_KEYS::SETTING_CONTINUITY_LO_OPEN, DEFAULT_CONTINUITY_LOW_OPEN_CIRCUIT);
    prefs.end();
    return val;
}

void StorageManager::setIgniterDuration(uint16_t ms) {
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putUShort(NVS_KEYS::SETTING_IGNITER_DURATION, ms);
    prefs.end();
}

void StorageManager::setAutoDelay(uint8_t sec) {
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putUChar(NVS_KEYS::SETTING_AUTO_DELAY, sec);
    prefs.end();
}

void StorageManager::setAbortOnDisconnect(bool flag) {
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putBool(NVS_KEYS::SETTING_ABORT_ON_DISCONNECT, flag);
    prefs.end();
}

void StorageManager::setEStopResetMode(EStopResetMode mode) {
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putUChar(NVS_KEYS::SETTING_ESTOP_RESET_MODE, static_cast<uint8_t>(mode));
    prefs.end();
}

void StorageManager::setBoardCount(uint8_t count) {
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putUChar(NVS_KEYS::SETTING_BOARD_COUNT, count);
    prefs.end();
}

void StorageManager::setContinuityThresholds(float loGood, float hiGood, float loOpen) {
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
    prefs.begin(NVS_KEYS::NS_AUX, true);
    const char* key = (relayIdx == 0) ? NVS_KEYS::AUX_RELAY_1_NAME : NVS_KEYS::AUX_RELAY_2_NAME;
    String name = prefs.getString(key, (relayIdx == 0) ? "Lights" : "Music");
    prefs.end();
    return name;
}

void StorageManager::setAuxRelayName(uint8_t relayIdx, const String& name) {
    prefs.begin(NVS_KEYS::NS_AUX, false);
    const char* key = (relayIdx == 0) ? NVS_KEYS::AUX_RELAY_1_NAME : NVS_KEYS::AUX_RELAY_2_NAME;
    prefs.putString(key, name);
    prefs.end();
}

// ============================================================================
// Import/Export
// ============================================================================

String StorageManager::exportShowJson() {
    StaticJsonDocument<8192> doc;  // Adjust size if needed for 48 zones
    
    // Zones array
    JsonArray zonesArray = doc.createNestedArray("zones");
    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        JsonObject zone = zonesArray.createNestedObject();
        zone["index"] = i;
        zone["description"] = getZoneDescription(i);
        zone["time"] = getZoneTime(i);
        zone["enabled"] = isZoneEnabled(i);
        zone["group"] = getZoneGroup(i);
        zone["order"] = getZoneOrder(i);
    }
    
    // Groups array
    JsonArray groupsArray = doc.createNestedArray("groups");
    for (uint8_t i = 0; i < 16; i++) {  // Arbitrary max groups
        String name = getGroupName(i);
        if (name.length() > 0) {
            JsonObject grp = groupsArray.createNestedObject();
            grp["id"] = i;
            grp["name"] = name;
            grp["members"] = getGroupMembers(i);
            grp["order"] = getGroupOrder(i);
        }
    }
    
    String jsonStr;
    serializeJson(doc, jsonStr);
    return jsonStr;
}

bool StorageManager::importShowJson(const String& jsonStr) {
    StaticJsonDocument<8192> doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    
    if (error) {
        Serial.printf("Import JSON parse error: %s\n", error.c_str());
        return false;
    }
    
    // Zones
    if (doc.containsKey("zones")) {
        JsonArray zonesArray = doc["zones"];
        for (JsonObject zone : zonesArray) {
            uint8_t idx = zone["index"];
            if (idx < MAX_ZONES) {
                if (zone.containsKey("description")) setZoneDescription(idx, zone["description"].as<String>());
                if (zone.containsKey("time")) setZoneTime(idx, zone["time"]);
                if (zone.containsKey("enabled")) setZoneEnabled(idx, zone["enabled"]);
                if (zone.containsKey("group")) setZoneGroup(idx, zone["group"]);
                if (zone.containsKey("order")) setZoneOrder(idx, zone["order"]);
            }
        }
    }
    
    // Groups
    if (doc.containsKey("groups")) {
        JsonArray groupsArray = doc["groups"];
        for (JsonObject grp : groupsArray) {
            uint8_t id = grp["id"];
            if (grp.containsKey("name")) setGroupName(id, grp["name"].as<String>());
            if (grp.containsKey("members")) setGroupMembers(id, grp["members"].as<String>());
            if (grp.containsKey("order")) setGroupOrder(id, grp["order"]);
        }
    }
    
    return true;
}

// ============================================================================
// Utilities
// ============================================================================

void StorageManager::clearAllZones() {
    prefs.begin(NVS_KEYS::NS_ZONES, false);
    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        prefs.remove(makeZoneKey(NVS_KEYS::ZONE_TIME_PREFIX, i).c_str());
        prefs.remove(makeZoneKey(NVS_KEYS::ZONE_DESC_PREFIX, i).c_str());
        prefs.remove(makeZoneKey(NVS_KEYS::ZONE_ENABLED_PREFIX, i).c_str());
        prefs.remove(makeZoneKey(NVS_KEYS::ZONE_GROUP_PREFIX, i).c_str());
        prefs.remove(makeZoneKey(NVS_KEYS::ZONE_ORDER_PREFIX, i).c_str());
    }
    prefs.end();
}

void StorageManager::resetToDefaults() {
    // Clear all namespaces
    prefs.begin(NVS_KEYS::NS_WIFI, false);
    prefs.clear();
    prefs.end();
    
    prefs.begin(NVS_KEYS::NS_ZONES, false);
    prefs.clear();
    prefs.end();
    
    prefs.begin(NVS_KEYS::NS_GROUPS, false);
    prefs.clear();
    prefs.end();
    
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.clear();
    prefs.end();
    
    prefs.begin(NVS_KEYS::NS_AUX, false);
    prefs.clear();
    prefs.end();
}

void StorageManager::saveSetting(const char* key, const String& value) {
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putString(key, value);
    prefs.end();
}

void StorageManager::saveSetting(const char* key, int value) {
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putInt(key, value);
    prefs.end();
}

void StorageManager::saveSetting(const char* key, bool value) {
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putBool(key, value);
    prefs.end();
}

void StorageManager::saveSetting(const char* key, float value) {
    prefs.begin(NVS_KEYS::NS_SETTINGS, false);
    prefs.putFloat(key, value);
    prefs.end();
}
