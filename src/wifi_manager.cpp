#include "wifi_manager.h"
#include "storage.h"
#include "websocket_handler.h"

WiFiManager wifiManager;

WiFiManager::WiFiManager()
    : apActive(false), clientConnected(false), clientConnectStartMs(0),
      connectState(WiFiConnectState::IDLE), connectStartMs(0),
      connectTimeoutMs(WIFI_CONNECT_TIMEOUT_MS), retryAtMs(0),
      connectRetryCount(0), bootAutoConnect(false) {
}

void WiFiManager::begin() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        this->onWiFiEvent(event);
    }, ARDUINO_EVENT_MAX);
    startAPMode();

    String clientSSID = storage.getClientSSID();
    String clientPass = storage.getClientPassword();
    if (clientSSID.length() > 0) {
        Serial.printf("[WiFi] Auto-connecting to saved network: %s\n", clientSSID.c_str());
        pendingConnectSsid = clientSSID;
        pendingConnectPass = clientPass;
        connectTimeoutMs = WIFI_BOOT_CONNECT_TIMEOUT_MS;
        connectRetryCount = 0;
        bootAutoConnect = true;
        beginConnectAttempt();
    }

    startMDNS();
}

void WiFiManager::startAPMode() {
    String ssid = storage.getApSSID();
    String password = storage.getApPassword();
    if (WiFi.softAP(ssid.c_str(), password.c_str())) {
        apActive = true;
        Serial.printf("AP Started: SSID=%s\n", ssid.c_str());
        Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    } else {
        Serial.println("[WiFi] Failed to start AP mode");
    }
}

void WiFiManager::stopAPMode() {
    if (apActive) {
        WiFi.softAPdisconnect(true);
        apActive = false;
        Serial.println("[WiFi] Hosted AP stopped");
    }
}

bool WiFiManager::isAPActive() {
    return apActive;
}

bool WiFiManager::connectToAccessPoint(const String& ssid, const String& password) {
    Serial.printf("[WiFi] Connecting (blocking) to: %s\n", ssid.c_str());
    clientConnectStartMs = millis();
    WiFi.begin(ssid.c_str(), password.c_str());
    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < WIFI_CONNECT_TIMEOUT_MS) {
        delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
        clientConnected = true;
        storage.setClientCredentials(ssid, password);
        Serial.printf("[WiFi] Connected! IP: %s — disabling hosted AP\n", WiFi.localIP().toString().c_str());
        stopAPMode();
        return true;
    } else {
        clientConnected = false;
        Serial.println("[WiFi] Auto-connect timed out, staying in AP mode");
        return false;
    }
}

void WiFiManager::scheduleConnect(const String& ssid, const String& password) {
    if (ssid.length() == 0) return;

    pendingConnectSsid = ssid;
    pendingConnectPass = password;
    connectTimeoutMs = WIFI_CONNECT_TIMEOUT_MS;
    connectRetryCount = 0;
    retryAtMs = 0;
    bootAutoConnect = false;
    beginConnectAttempt();
}

bool WiFiManager::isConnecting() const {
    return connectState == WiFiConnectState::CONNECTING || connectState == WiFiConnectState::BACKOFF;
}

void WiFiManager::update() {
    if (connectState == WiFiConnectState::BACKOFF) {
        if (millis() >= retryAtMs) {
            Serial.println("[WiFi] Retry backoff timeout, attempting connect");
            beginConnectAttempt();
        }
        return;
    }

    if (connectState != WiFiConnectState::CONNECTING) return;

    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
        completeConnectionSuccess();
        return;
    }

    if (millis() - connectStartMs > connectTimeoutMs) {
        Serial.printf("[WiFi] Connect timeout after %lums, status=%d\n", millis() - connectStartMs, status);
        if (WiFi.status() == WL_CONNECTED) {
            completeConnectionSuccess();
            return;
        }

        clientConnected = false;
        scheduleRetry();
    }
}

void WiFiManager::beginConnectAttempt() {
    if (pendingConnectSsid.length() == 0) {
        connectState = WiFiConnectState::FAILED;
        return;
    }

    connectStartMs = millis();
    connectState = WiFiConnectState::CONNECTING;
    WiFi.disconnect(false, false);
    WiFi.begin(pendingConnectSsid.c_str(), pendingConnectPass.c_str());
    Serial.printf("[WiFi] Non-blocking connect started: %s (timeout=%lums)\n", pendingConnectSsid.c_str(), connectTimeoutMs);
}

void WiFiManager::completeConnectionSuccess() {
    clientConnected = true;
    if (pendingConnectSsid.length() > 0) {
        storage.setClientCredentials(pendingConnectSsid, pendingConnectPass);
    }

    connectState = WiFiConnectState::IDLE;
    connectRetryCount = 0;
    retryAtMs = 0;
    bootAutoConnect = false;

    Serial.printf("[WiFi] Connected! IP: %s — disabling hosted AP\n", WiFi.localIP().toString().c_str());
    stopAPMode();
    wsHandler.broadcastFullState();
    wsHandler.broadcastWiFiStatus();
}

void WiFiManager::scheduleRetry() {
    if (connectRetryCount >= WIFI_CONNECT_MAX_RETRIES) {
        connectState = WiFiConnectState::FAILED;
        WiFi.disconnect(false, false);
        startAPMode();
        Serial.println("[WiFi] Connect failed after retries — hosted AP remains active");
        wsHandler.broadcastError("WIFI_CONNECT_FAILED", "Could not connect — check SSID and password");
        wsHandler.broadcastFullState();
        return;
    }

    connectRetryCount++;
    uint32_t backoffMs = WIFI_CONNECT_RETRY_BASE_MS * (1UL << (connectRetryCount - 1));
    if (backoffMs > 30000) {
        backoffMs = 30000;
    }

    connectState = WiFiConnectState::BACKOFF;
    retryAtMs = millis() + backoffMs;
    startAPMode();
    Serial.printf("[WiFi] Connect timeout, scheduling retry %u/%u in %lums\n", connectRetryCount, WIFI_CONNECT_MAX_RETRIES, backoffMs);

    if (!bootAutoConnect) {
        wsHandler.broadcastError("WIFI_CONNECT_RETRY", "Connection delayed — retrying automatically");
    }
    wsHandler.broadcastFullState();
}

void WiFiManager::disconnectFromAccessPoint() {
    WiFi.disconnect(false, false);
    clientConnected = false;
    connectState = WiFiConnectState::IDLE;
    connectRetryCount = 0;
    retryAtMs = 0;
    Serial.println("[WiFi] Disconnected from external AP");
}

void WiFiManager::forgetNetwork() {
    storage.setClientCredentials("", "");
    if (isClientConnected()) WiFi.disconnect(false, false);
    connectState = WiFiConnectState::IDLE;
    connectRetryCount = 0;
    retryAtMs = 0;
    bootAutoConnect = false;
    pendingConnectSsid = "";
    pendingConnectPass = "";
    clientConnected = false;
    Serial.println("[WiFi] Network forgotten — re-enabling hosted AP");
    startAPMode();
}

bool WiFiManager::isClientConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

String WiFiManager::getCurrentSSID() {
    if (isClientConnected()) return WiFi.SSID();
    return storage.getApSSID();
}

void WiFiManager::startMDNS() {
    if (MDNS.begin("apexpyro")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("mDNS responder started (apexpyro.local)");
    } else {
        Serial.println("[WiFi] Error starting mDNS responder");
    }
}

void WiFiManager::stopMDNS() { MDNS.end(); }

WiFiLevel WiFiManager::getRSSILevel() {
    if (!isClientConnected()) return WiFiLevel::NONE_LEVEL;
    int8_t rssi = WiFi.RSSI();
    if (rssi >= WIFI_RSSI_LEVEL_HIGH) return WiFiLevel::HIGH_LEVEL;
    if (rssi >= WIFI_RSSI_LEVEL_MEDIUM) return WiFiLevel::MEDIUM_LEVEL;
    if (rssi >= WIFI_RSSI_LEVEL_LOW) return WiFiLevel::LOW_LEVEL;
    return WiFiLevel::NONE_LEVEL;
}

int8_t WiFiManager::getRSSI() { return WiFi.RSSI(); }

void WiFiManager::onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println("[WiFi] STA Connected");
            clientConnected = true;
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("[WiFi] STA Disconnected");
            clientConnected = false;
            if (connectState == WiFiConnectState::CONNECTING) {
                scheduleRetry();
            } else {
                handleClientDisconnect();
            }
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[WiFi] STA Got IP: %s\n", WiFi.localIP().toString().c_str());
            clientConnected = true;
            completeConnectionSuccess();
            break;
        case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
            Serial.println("[WiFi] AP: Client connected");
            break;
        case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
            Serial.println("[WiFi] AP: Client disconnected");
            break;
        default:
            break;
    }
}

void WiFiManager::handleClientDisconnect() {
    if (!apActive && connectState == WiFiConnectState::IDLE) {
        startAPMode();
    }
}

void WiFiManager::fallbackToAPMode() {
    if (!apActive) startAPMode();
}

String WiFiManager::getMacAddress() { return WiFi.macAddress(); }

String WiFiManager::getLocalIP() {
    if (isClientConnected()) return WiFi.localIP().toString();
    return "N/A";
}

String WiFiManager::getAPIP() { return WiFi.softAPIP().toString(); }
