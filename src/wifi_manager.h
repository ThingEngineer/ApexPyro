#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <vector>
#include "config.h"

class WiFiManager {
public:
    WiFiManager();
    
    // Initialization
    void begin();
    
    // Access Point Mode (STA-AP hybrid)
    void startAPMode();
    void stopAPMode();
    bool isAPActive();
    
    // Client Connection
    bool connectToAccessPoint(const String& ssid, const String& password);
    void disconnectFromAccessPoint();
    bool isClientConnected();
    String getCurrentSSID();
    
    // mDNS
    void startMDNS();
    void stopMDNS();
    
    // WiFi Scanning
    void startWiFiScan();
    bool isScanComplete();
    std::vector<String> getScanResults();  // Returns SSIDs
    
    // Signal Strength
    WiFiLevel getRSSILevel();
    int8_t getRSSI();
    
    // Event Handling
    void onWiFiEvent(WiFiEvent_t event);
    
    // Utilities
    String getMacAddress();
    String getLocalIP();
    String getAPIP();
    
private:
    bool apActive;
    bool clientConnected;
    bool scanInProgress;
    std::vector<String> scanResults;
    uint32_t clientConnectStartMs;
    
    void handleClientDisconnect();
    void fallbackToAPMode();
};

extern WiFiManager wifiManager;

#endif  // WIFI_MANAGER_H
