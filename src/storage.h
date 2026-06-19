#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <Preferences.h>
#include <vector>
#include "config.h"

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
    
    // Getters - Groups
    String getGroupName(uint8_t groupIdx);
    String getGroupMembers(uint8_t groupIdx);  // Comma-separated zone indices
    uint8_t getGroupOrder(uint8_t groupIdx);
    
    void setGroupName(uint8_t groupIdx, const String& name);
    void setGroupMembers(uint8_t groupIdx, const String& members);  // Comma-separated
    void setGroupOrder(uint8_t groupIdx, uint8_t order);
    
    // Getters - Settings
    uint16_t getIgniterDuration();
    uint8_t getAutoDelay();
    bool getAbortOnDisconnect();
    EStopResetMode getEStopResetMode();
    uint8_t getBoardCount();
    float getContinuityLoGood();
    float getContinuityHiGood();
    float getContinuityLoOpen();
    
    void setIgniterDuration(uint16_t ms);
    void setAutoDelay(uint8_t sec);
    void setAbortOnDisconnect(bool flag);
    void setEStopResetMode(EStopResetMode mode);
    void setBoardCount(uint8_t count);
    void setContinuityThresholds(float loGood, float hiGood, float loOpen);
    
    // Getters - Aux
    String getAuxRelayName(uint8_t relayIdx);  // relayIdx = 0 or 1
    void setAuxRelayName(uint8_t relayIdx, const String& name);
    
    // Import/Export
    String exportShowJson();
    bool importShowJson(const String& jsonStr);
    
    // Utilities
    void clearAllZones();  // Reset all zone times/descriptions
    void resetToDefaults();  // Full factory reset
    
private:
    Preferences prefs;
    
    String makeZoneKey(const char* prefix, uint8_t zoneIdx);
    String makeGroupKey(const char* prefix, uint8_t groupIdx);
};

extern StorageManager storage;

#endif  // STORAGE_H
