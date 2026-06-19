#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "config.h"

enum class WiFiConnectState : uint8_t {
    IDLE = 0,
    CONNECTING = 1,
    BACKOFF = 2,
    FAILED = 3,
};

class WiFiManager {
public:
    WiFiManager();

    // Initialization & per-loop update
    void begin();
    void update();   // Drives the non-blocking connect state machine

    // Access Point Mode
    void startAPMode();
    void stopAPMode();
    bool isAPActive();

    // Client Connection
    // Blocking — only called at boot from begin()
    bool connectToAccessPoint(const String& ssid, const String& password);
    // Non-blocking — called from WebSocket handler; result delivered via update() → broadcastFullState()
    void scheduleConnect(const String& ssid, const String& password);
    bool isConnecting() const;
    void disconnectFromAccessPoint();
    bool isClientConnected();
    String getCurrentSSID();
    void forgetNetwork();

    // mDNS
    void startMDNS();
    void stopMDNS();

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
    uint32_t clientConnectStartMs;

    // Non-blocking connect state machine
    WiFiConnectState connectState;
    uint32_t connectStartMs;
    uint32_t connectTimeoutMs;
    uint32_t retryAtMs;
    uint8_t connectRetryCount;
    bool bootAutoConnect;
    String pendingConnectSsid;
    String pendingConnectPass;

    void handleClientDisconnect();
    void fallbackToAPMode();
    void beginConnectAttempt();
    void completeConnectionSuccess();
    void scheduleRetry();
};

extern WiFiManager wifiManager;

#endif  // WIFI_MANAGER_H
