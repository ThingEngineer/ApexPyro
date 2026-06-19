#include "wifi_manager.h"
#include "storage.h"

WiFiManager wifiManager;

WiFiManager::WiFiManager() 
    : apActive(false), clientConnected(false), scanInProgress(false), clientConnectStartMs(0) {
}

void WiFiManager::begin() {
    // Set WiFi mode to AP + STA (hybrid mode)
    WiFi.mode(WIFI_AP_STA);
    
    // Register event listener using WiFi.onEvent with proper callback
    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
        this->onWiFiEvent(event);
    }, ARDUINO_EVENT_MAX);
    
    // Start AP mode with saved credentials
    startAPMode();
    
    // Try to connect to saved client network if available
    String clientSSID = storage.getClientSSID();
    String clientPass = storage.getClientPassword();
    if (clientSSID.length() > 0) {
        Serial.printf("Attempting WiFi client mode to: %s\n", clientSSID.c_str());
        connectToAccessPoint(clientSSID, clientPass);
    }
    
    // Start mDNS
    startMDNS();
}

void WiFiManager::startAPMode() {
    String ssid = storage.getApSSID();
    String password = storage.getApPassword();
    
    if (WiFi.softAP(ssid.c_str(), password.c_str())) {
        apActive = true;
        Serial.printf("AP Started: SSID=%s\n", ssid.c_str());
        IPAddress apIP = WiFi.softAPIP();
        Serial.printf("AP IP: %s\n", apIP.toString().c_str());
    } else {
        Serial.println("Failed to start AP mode");
    }
}

void WiFiManager::stopAPMode() {
    if (apActive) {
        WiFi.softAPdisconnect(true);
        apActive = false;
        Serial.println("AP stopped");
    }
}

bool WiFiManager::isAPActive() {
    return apActive;
}

bool WiFiManager::connectToAccessPoint(const String& ssid, const String& password) {
    Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());
    
    clientConnectStartMs = millis();
    WiFi.begin(ssid.c_str(), password.c_str());
    
    // Wait for connection with timeout
    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < WIFI_CONNECT_TIMEOUT_MS) {
        delay(100);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        clientConnected = true;
        storage.setClientCredentials(ssid, password);
        Serial.printf("Connected to WiFi! IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    } else {
        Serial.println("WiFi connection timeout, reverting to AP mode");
        clientConnected = false;
        fallbackToAPMode();
        return false;
    }
}

void WiFiManager::disconnectFromAccessPoint() {
    if (clientConnected) {
        WiFi.disconnect(true);  // true = turn off station mode
        clientConnected = false;
        storage.setClientCredentials("", "");  // Clear saved credentials
        Serial.println("Disconnected from WiFi");
    }
}

bool WiFiManager::isClientConnected() {
    return (WiFi.status() == WL_CONNECTED);
}

String WiFiManager::getCurrentSSID() {
    if (isClientConnected()) {
        return WiFi.SSID();
    }
    return storage.getApSSID();
}

void WiFiManager::startMDNS() {
    if (MDNS.begin("apexpyro")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("mDNS responder started (apexpyro.local)");
    } else {
        Serial.println("Error starting mDNS responder");
    }
}

void WiFiManager::stopMDNS() {
    MDNS.end();
}

void WiFiManager::startWiFiScan() {
    if (!scanInProgress) {
        scanInProgress = true;
        scanResults.clear();
        Serial.println("Starting WiFi scan...");
        WiFi.scanNetworks(true);  // Async scan
    }
}

bool WiFiManager::isScanComplete() {
    int8_t scanResult = WiFi.scanComplete();
    
    if (scanResult == WIFI_SCAN_RUNNING) {
        return false;  // Still scanning
    }
    
    if (scanResult > 0) {
        // Scan completed successfully
        scanResults.clear();
        for (int i = 0; i < scanResult; i++) {
            scanResults.push_back(WiFi.SSID(i));
        }
        scanInProgress = false;
        WiFi.scanDelete();
        return true;
    }
    
    if (scanResult == WIFI_SCAN_FAILED) {
        scanInProgress = false;
        Serial.println("WiFi scan failed");
        return true;  // Still return true to indicate scan completed (even if failed)
    }
    
    return false;
}

std::vector<String> WiFiManager::getScanResults() {
    return scanResults;
}

WiFiLevel WiFiManager::getRSSILevel() {
    if (!isClientConnected()) {
        return WiFiLevel::NONE_LEVEL;
    }
    
    int8_t rssi = WiFi.RSSI();
    
    if (rssi >= WIFI_RSSI_LEVEL_HIGH) {
        return WiFiLevel::HIGH_LEVEL;
    } else if (rssi >= WIFI_RSSI_LEVEL_MEDIUM) {
        return WiFiLevel::MEDIUM_LEVEL;
    } else if (rssi >= WIFI_RSSI_LEVEL_LOW) {
        return WiFiLevel::LOW_LEVEL;
    } else {
        return WiFiLevel::NONE_LEVEL;
    }
}

int8_t WiFiManager::getRSSI() {
    return WiFi.RSSI();
}

void WiFiManager::onWiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            Serial.println("[WiFi] STA Connected");
            clientConnected = true;
            break;
            
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("[WiFi] STA Disconnected");
            clientConnected = false;
            handleClientDisconnect();
            break;
            
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[WiFi] STA Got IP: %s\n", WiFi.localIP().toString().c_str());
            clientConnected = true;
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
    // If client WiFi disconnects and we still have AP, we stay on AP
    // If we lose both, that's a bigger issue (WiFi hardware problem)
    if (!apActive) {
        startAPMode();
    }
}

void WiFiManager::fallbackToAPMode() {
    if (!apActive) {
        startAPMode();
    }
}

String WiFiManager::getMacAddress() {
    return WiFi.macAddress();
}

String WiFiManager::getLocalIP() {
    if (isClientConnected()) {
        return WiFi.localIP().toString();
    }
    return "N/A";
}

String WiFiManager::getAPIP() {
    return WiFi.softAPIP().toString();
}
