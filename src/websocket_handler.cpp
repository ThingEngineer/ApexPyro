#include "websocket_handler.h"
#include "storage.h"
#include "wifi_manager.h"
#include "relay_manager.h"
#include "continuity.h"
#include <LittleFS.h>

WebSocketHandler wsHandler;

// WebSocketHandler implementation starts here


WebSocketHandler::WebSocketHandler()
    : server(80), ws("/ws"), controllerClientId(0), lastHeartbeatMs(0) {
}

uint32_t WebSocketHandler::calculateCrc32(const String& data) {
    uint32_t crc = 0xffffffff;
    for (size_t i = 0; i < data.length(); i++) {
        uint8_t byte = data[i];
        for (int j = 0; j < 8; j++) {
            uint32_t bit = (byte >> (7-j)) & 1;
            uint32_t c = (crc >> 31) & 1;
            crc = crc << 1;
            if (c ^ bit) crc = crc ^ 0x04c11db7;
        }
    }
    return crc;
}

bool WebSocketHandler::validateChecksum(const char* command, uint8_t zone, uint32_t timestamp, const char* checksum) {
    // Build the string that was checksummed: "cmd:zone:timestamp"
    String data = String(command) + ":" + String(zone) + ":" + String(timestamp);
    uint32_t computed = calculateCrc32(data);
    
    // Convert computed CRC to hex and compare
    char hexStr[9];
    sprintf(hexStr, "%08x", computed);
    
    return strcmp(hexStr, checksum) == 0;
}

void WebSocketHandler::begin() {
    // Initialize LittleFS for serving static files
    if (!LittleFS.begin()) {
        Serial.println("[WebSocketHandler] Failed to mount LittleFS");
        return;
    }
    
    // Serve static files from /data/
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("max-age=600");
    
    // WebSocket endpoint
    ws.onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
        this->onWsEvent(server, client, type, arg, data, len);
    });
    server.addHandler(&ws);
    
    // Start server
    server.begin();
    Serial.println("[WebSocketHandler] WebSocket server started on port 80");
    
    lastHeartbeatMs = millis();
}

void WebSocketHandler::update() {
    uint32_t now = millis();
    
    // Send heartbeat every 5 seconds
    if (now - lastHeartbeatMs >= WEBSOCKET_HEARTBEAT_INTERVAL_MS) {
        sendHeartbeat();
        lastHeartbeatMs = now;
    }
    
    // Broadcast periodic status
    static uint32_t lastBroadcast = 0;
    if (now - lastBroadcast >= 1000) {
        broadcastContinuity();
        broadcastBatteryStatus();
        broadcastWiFiStatus();
        lastBroadcast = now;
    }
}

void WebSocketHandler::onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        uint32_t clientId = client->id();
        Serial.printf("[WebSocketHandler] Client connected: %u\n", clientId);
        
        // Assign role
        if (wsHandler.controllerClientId == 0) {
            // First client becomes controller
            wsHandler.controllerClientId = clientId;
            Serial.printf("[WebSocketHandler] Client %u assigned CONTROLLER role\n", clientId);
        } else {
            // Subsequent clients are viewers
            wsHandler.viewerClientIds.push_back(clientId);
            Serial.printf("[WebSocketHandler] Client %u assigned VIEWER role\n", clientId);
        }
        
        // Send full state to new client
        wsHandler.broadcastFullState();
        wsHandler.broadcastRoleAssignment();
        
    } else if (type == WS_EVT_DISCONNECT) {
        uint32_t clientId = client->id();
        Serial.printf("[WebSocketHandler] Client disconnected: %u\n", clientId);
        
        wsHandler.removeClient(clientId);
        
        if (wsHandler.controllerClientId == clientId) {
            wsHandler.controllerClientId = 0;
            wsHandler.promoteViewerToController();
            wsHandler.broadcastRoleAssignment();
        }
        
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo* info = (AwsFrameInfo*)arg;
        uint32_t clientId = client->id();
        
        if (info->final && info->index == 0 && info->len == len) {
            // Complete message
            data[len] = 0;  // Null-terminate
            
            StaticJsonDocument<512> doc;
            DeserializationError error = deserializeJson(doc, (const char*)data);
            
            if (error) {
                Serial.printf("[WebSocketHandler] JSON parse error: %s\n", error.c_str());
                return;
            }
            
            const char* msgType = doc["type"] | "";
            
            // Route message based on type
            if (strcmp(msgType, "fire") == 0) {
                wsHandler.handleFireCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "arm") == 0) {
                wsHandler.handleArmCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "aux") == 0) {
                wsHandler.handleAuxCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "estop") == 0) {
                wsHandler.handleEStopCommand(clientId);
            } else if (strcmp(msgType, "estop_reset") == 0) {
                wsHandler.handleEStopReset(clientId);
            } else if (strcmp(msgType, "auto_start") == 0) {
                wsHandler.handleAutoStartCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "auto_stop") == 0) {
                wsHandler.handleAutoStopCommand(clientId);
            } else if (strcmp(msgType, "set_zone") == 0) {
                wsHandler.handleZoneConfigCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "set_group") == 0) {
                wsHandler.handleGroupConfigCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "set_setting") == 0) {
                wsHandler.handleSettingCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "set_aux_name") == 0) {
                wsHandler.handleAuxNameCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "wifi_scan") == 0) {
                wsHandler.handleWiFiScanCommand(clientId);
            } else if (strcmp(msgType, "wifi_connect") == 0) {
                wsHandler.handleWiFiConnectCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "import_show") == 0) {
                wsHandler.handleImportShowCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "relay_test") == 0) {
                wsHandler.handleRelayTestCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "pong") == 0) {
                // Heartbeat response
            }
        }
    }
}

void WebSocketHandler::removeClient(uint32_t clientId) {
    for (auto it = viewerClientIds.begin(); it != viewerClientIds.end(); ++it) {
        if (*it == clientId) {
            viewerClientIds.erase(it);
            break;
        }
    }
}

void WebSocketHandler::promoteViewerToController() {
    if (!viewerClientIds.empty()) {
        controllerClientId = viewerClientIds.front();
        viewerClientIds.erase(viewerClientIds.begin());
        Serial.printf("[WebSocketHandler] Promoted viewer %u to CONTROLLER\n", controllerClientId);
    }
}

void WebSocketHandler::sendHeartbeat() {
    StaticJsonDocument<64> doc;
    doc["type"] = "ping";
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void WebSocketHandler::broadcastFullState() {
    StaticJsonDocument<2048> doc;
    
    doc["type"] = "full_state";
    doc["masterArmed"] = relayManager.isMasterArmed();
    doc["boardCount"] = relayManager.boardPresentCount;
    
    // Zone array would go here (simplified for now)
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void WebSocketHandler::broadcastContinuity() {
    StaticJsonDocument<768> doc;
    
    doc["type"] = "continuity";
    JsonArray zones = doc.createNestedArray("zones");
    
    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        ContinuityStatus status = continuityManager.getZoneStatus(i);
        zones.add((uint8_t)status);
    }
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void WebSocketHandler::broadcastBatteryStatus() {
    StaticJsonDocument<128> doc;
    
    doc["type"] = "battery";
    doc["percent"] = continuityManager.getBatteryPercent();
    doc["voltage"] = continuityManager.getBatteryVoltage();
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void WebSocketHandler::broadcastWiFiStatus() {
    StaticJsonDocument<256> doc;
    
    doc["type"] = "wifi";
    doc["level"] = (uint8_t)wifiManager.getRSSILevel();
    doc["ssid"] = wifiManager.getCurrentSSID();
    doc["connected"] = wifiManager.isClientConnected();
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void WebSocketHandler::broadcastZoneFired(uint8_t zoneIdx, uint32_t duration) {
    StaticJsonDocument<128> doc;
    
    doc["type"] = "zone_fired";
    doc["zone"] = zoneIdx;
    doc["duration"] = duration;
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void WebSocketHandler::broadcastShowProgress(uint8_t currentStep, uint8_t totalSteps, uint8_t currentZone) {
    StaticJsonDocument<128> doc;
    
    doc["type"] = "show_progress";
    doc["step"] = currentStep;
    doc["total"] = totalSteps;
    doc["zone"] = currentZone;
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void WebSocketHandler::broadcastEStop() {
    StaticJsonDocument<64> doc;
    
    doc["type"] = "estop_active";
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void WebSocketHandler::broadcastError(const char* code, const char* message) {
    StaticJsonDocument<256> doc;
    
    doc["type"] = "error";
    doc["code"] = code;
    doc["msg"] = message;
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void WebSocketHandler::broadcastRoleAssignment() {
    // Send role to controller
    if (controllerClientId != 0) {
        StaticJsonDocument<128> doc;
        doc["type"] = "role";
        doc["role"] = "controller";
        
        String json;
        serializeJson(doc, json);
        
        AsyncWebSocketClient* client = ws.client(controllerClientId);
        if (client) {
            client->text(json);
        }
    }
    
    // Send role to all viewers
    for (uint32_t clientId : viewerClientIds) {
        StaticJsonDocument<128> doc;
        doc["type"] = "role";
        doc["role"] = "viewer";
        
        String json;
        serializeJson(doc, json);
        
        AsyncWebSocketClient* client = ws.client(clientId);
        if (client) {
            client->text(json);
        }
    }
}

void WebSocketHandler::broadcastSystemStatus() {
    StaticJsonDocument<256> doc;
    
    doc["type"] = "system_status";
    doc["masterArmed"] = relayManager.isMasterArmed();
    doc["zonesFiring"] = relayManager.isZoneFiring();
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

// ============================================================================
// Message Handlers (Stubs for now - will be filled in fully)
// ============================================================================

void WebSocketHandler::handleFireCommand(uint32_t clientId, const char* data) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can fire");
        return;
    }
    
    StaticJsonDocument<256> doc;
    deserializeJson(doc, data);
    
    uint8_t zone = doc["zone"] | 0;
    uint32_t ts = doc["ts"] | 0;
    const char* cs = doc["cs"] | "";
    
    if (!validateChecksum("FIRE", zone, ts, cs)) {
        broadcastError("BAD_CHECKSUM", "Checksum validation failed");
        return;
    }
    
    uint16_t ignitersOnDuration = storage.getIgniterDuration();
    relayManager.startZoneFire(zone, ignitersOnDuration);
    broadcastZoneFired(zone, ignitersOnDuration);
}

void WebSocketHandler::handleArmCommand(uint32_t clientId, const char* data) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can arm");
        return;
    }
    
    StaticJsonDocument<256> doc;
    deserializeJson(doc, data);
    
    bool state = doc["state"] | false;
    uint32_t ts = doc["ts"] | 0;
    const char* cs = doc["cs"] | "";
    
    if (!validateChecksum("ARM", state ? 1 : 0, ts, cs)) {
        broadcastError("BAD_CHECKSUM", "Checksum validation failed");
        return;
    }
    
    relayManager.setMasterArm(state);
    broadcastSystemStatus();
}

void WebSocketHandler::handleAuxCommand(uint32_t clientId, const char* data) {
    StaticJsonDocument<256> doc;
    deserializeJson(doc, data);
    
    uint8_t relay = doc["relay"] | 0;
    bool state = doc["state"] | false;
    
    relayManager.setAuxRelay(relay, state);
    broadcastSystemStatus();
}

void WebSocketHandler::handleEStopCommand(uint32_t clientId) {
    // E-Stop is allowed from any client (emergency override)
    relayManager.setMasterArm(false);
    relayManager.setAllRelaysOff();
    broadcastEStop();
}

void WebSocketHandler::handleEStopReset(uint32_t clientId) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can reset E-Stop");
        return;
    }
    
    broadcastSystemStatus();
}

void WebSocketHandler::handleAutoStartCommand(uint32_t clientId, const char* data) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can start auto show");
        return;
    }
    
    // Delegate to show runner (when implemented)
    // For now, just broadcast
}

void WebSocketHandler::handleAutoStopCommand(uint32_t clientId) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can stop auto show");
        return;
    }
}

void WebSocketHandler::handleZoneConfigCommand(uint32_t clientId, const char* data) {
    StaticJsonDocument<512> doc;
    deserializeJson(doc, data);
    
    uint8_t zone = doc["zone"] | 0;
    if (zone < MAX_ZONES) {
        if (doc.containsKey("desc")) storage.setZoneDescription(zone, doc["desc"].as<String>());
        if (doc.containsKey("time")) storage.setZoneTime(zone, doc["time"]);
        if (doc.containsKey("enabled")) storage.setZoneEnabled(zone, doc["enabled"]);
        if (doc.containsKey("group")) storage.setZoneGroup(zone, doc["group"]);
        if (doc.containsKey("order")) storage.setZoneOrder(zone, doc["order"]);
    }
}

void WebSocketHandler::handleGroupConfigCommand(uint32_t clientId, const char* data) {
    StaticJsonDocument<512> doc;
    deserializeJson(doc, data);
    
    uint8_t groupId = doc["id"] | 0;
    if (doc.containsKey("name")) storage.setGroupName(groupId, doc["name"].as<String>());
    if (doc.containsKey("members")) storage.setGroupMembers(groupId, doc["members"].as<String>());
    if (doc.containsKey("order")) storage.setGroupOrder(groupId, doc["order"]);
}

void WebSocketHandler::handleSettingCommand(uint32_t clientId, const char* data) {
    // Save generic settings
    StaticJsonDocument<512> doc;
    deserializeJson(doc, data);
    
    const char* key = doc["key"] | "";
    const char* value = doc["value"] | "";
    
    storage.saveSetting(key, String(value));
}

void WebSocketHandler::handleAuxNameCommand(uint32_t clientId, const char* data) {
    StaticJsonDocument<256> doc;
    deserializeJson(doc, data);
    
    uint8_t relay = doc["relay"] | 0;
    const char* name = doc["name"] | "";
    
    storage.setAuxRelayName(relay, name);
}

void WebSocketHandler::handleWiFiScanCommand(uint32_t clientId) {
    wifiManager.startWiFiScan();
}

void WebSocketHandler::handleWiFiConnectCommand(uint32_t clientId, const char* data) {
    StaticJsonDocument<256> doc;
    deserializeJson(doc, data);
    
    const char* ssid = doc["ssid"] | "";
    const char* pass = doc["pass"] | "";
    
    wifiManager.connectToAccessPoint(ssid, pass);
}

void WebSocketHandler::handleImportShowCommand(uint32_t clientId, const char* data) {
    StaticJsonDocument<8192> doc;
    deserializeJson(doc, data);
    
    if (doc.containsKey("data")) {
        String jsonData;
        serializeJson(doc["data"], jsonData);
        storage.importShowJson(jsonData);
    }
}

void WebSocketHandler::handleRelayTestCommand(uint32_t clientId, const char* data) {
    if (!relayManager.isMasterArmed()) {
        broadcastError("NOT_ARMED", "System must be armed for relay test");
        return;
    }
    
    // Run sequential relay test
    // This will be implemented with the show runner
}
