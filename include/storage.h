#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <vector>
#include "config.h"

// Compact in-memory representation of a single zone.
struct ZoneData {
    String  description;
    float   time;
    bool    enabled;
    uint8_t group;
    uint8_t order;
};

class StorageManager {
public:
    StorageManager();
    
    // Initialization
    void begin();
    
    // Load/Save Operations
    void loadAll();
    void saveZone(uint8_t zoneIdx);
    void saveSetting(const char* key, const String& value);
    void saveSetting(const char* key, int value);
    void saveSetting(const char* key, bool value);
    void saveSetting(const char* key, float value);
    
    // Getters - WiFi
    String getApSSID();
    String getApPassword();
    String getClientSSID();
    String getClientPassword();
    void setApCredentials(const String& ssid, const String& pass);
    void setClientCredentials(const String& ssid, const String& pass);
    
    // Getters - Zones
    String getZoneDescription(uint8_t zoneIdx);
    float getZoneTime(uint8_t zoneIdx);
    bool isZoneEnabled(uint8_t zoneIdx);
    uint8_t getZoneGroup(uint8_t zoneIdx);
    uint8_t getZoneOrder(uint8_t zoneIdx);
    
    void setZoneDescription(uint8_t zoneIdx, const String& desc);
    void setZoneTime(uint8_t zoneIdx, float timeSeconds);
    void setZoneEnabled(uint8_t zoneIdx, bool enabled);
    void setZoneGroup(uint8_t zoneIdx, uint8_t groupIdx);
    void setZoneOrder(uint8_t zoneIdx, uint8_t order);

    // Batch-writes all five zone fields in a single NVS open/close transaction.
    void setZoneBatch(uint8_t zoneIdx, const String& desc, float time,
                      bool enabled, uint8_t group, uint8_t order);
    
    // Getters - Settings
    uint16_t getIgniterDuration();
    uint8_t getAutoDelay();
    bool getAbortOnDisconnect();
    EStopResetMode getEStopResetMode();
    uint8_t getBoardCount();
    float getContinuityLoGood();
    float getContinuityHiGood();
    float getContinuityLoOpen();
    uint8_t getBatteryProfileId();
    uint8_t getBatteryCellCount();
    float getBatteryPackMin();
    float getBatteryPackMax();
    float getBatteryLowWarn();
    uint16_t getBatterySampleIntervalMs();
    uint8_t getBatteryCurvePointCount();
    void getBatteryCurve(BatteryCurvePoint* points, uint8_t& count);
    
    void setIgniterDuration(uint16_t ms);
    void setAutoDelay(uint8_t sec);
    void setAbortOnDisconnect(bool flag);
    void setEStopResetMode(EStopResetMode mode);
    void setBoardCount(uint8_t count);
    void setContinuityThresholds(float loGood, float hiGood, float loOpen);
    void setBatteryProfileId(uint8_t profileId);
    void setBatteryCellCount(uint8_t count);
    void setBatteryPackMin(float voltage);
    void setBatteryPackMax(float voltage);
    void setBatteryLowWarn(float voltage);
    void setBatterySampleIntervalMs(uint16_t intervalMs);
    void setBatteryCurve(const BatteryCurvePoint* points, uint8_t count);
    
    // Getters - Aux
    String getAuxRelayName(uint8_t relayIdx);  // relayIdx = 0..AUX_RELAY_COUNT-1
    void setAuxRelayName(uint8_t relayIdx, const String& name);
    
    // Import/Export
    String exportShowJson();
    bool importShowJson(const String& jsonStr);
    String exportSettingsJson();
    bool importSettingsJson(const String& jsonStr);
    
    // Utilities
    void clearAllZones();  // Reset all zone times/descriptions
    void resetToDefaults();  // Full factory reset
    
private:
    Preferences prefs;

    // Zone data is kept in a RAM cache and persisted as /zones.json on LittleFS.
    // This avoids NVS exhaustion from 240+ individual zone keys.
    ZoneData    zoneCache[MAX_ZONES];
    bool        zoneCacheLoaded;

    void initZoneDefaults();
    bool loadZonesFromFile();
    bool saveZonesToFile();

    String makeZoneKey(const char* prefix, uint8_t zoneIdx);
};

extern StorageManager storage;

#endif  // STORAGE_H
