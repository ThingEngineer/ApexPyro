#include "websocket_handler.h"
#include "storage.h"
#include "wifi_manager.h"
#include "relay_manager.h"
#include "continuity.h"
#include "show_runner.h"
#include <LittleFS.h>

namespace {
static const uint8_t MAX_GROUPS = 16;
}

WebSocketHandler wsHandler;

// WebSocketHandler implementation starts here


WebSocketHandler::WebSocketHandler()
    : server(80), ws("/ws"), controllerClientId(0), controllerRoleLocked(true), controllerOwnerKey(""), lastHeartbeatMs(0), relayTestActive(false), relayTestStepIdx(0), relayTestStepStartMs(0), relayTestPulseMs(0) {
}

bool WebSocketHandler::hasViewer(uint32_t clientId) const {
    for (uint32_t viewerId : viewerClientIds) {
        if (viewerId == clientId) {
            return true;
        }
    }
    return false;
}

void WebSocketHandler::addViewer(uint32_t clientId) {
    if (clientId == 0 || hasViewer(clientId)) {
        return;
    }
    viewerClientIds.push_back(clientId);
}

void WebSocketHandler::removeViewer(uint32_t clientId) {
    for (auto it = viewerClientIds.begin(); it != viewerClientIds.end(); ++it) {
        if (*it == clientId) {
            viewerClientIds.erase(it);
            break;
        }
    }
}

void WebSocketHandler::setClientIdentity(uint32_t clientId, const String& key) {
    for (auto& identity : clientIdentities) {
        if (identity.clientId == clientId) {
            identity.key = key;
            return;
        }
    }

    clientIdentities.push_back({clientId, key});
}

String WebSocketHandler::getClientIdentity(uint32_t clientId) const {
    for (const auto& identity : clientIdentities) {
        if (identity.clientId == clientId) {
            return identity.key;
        }
    }
    return "";
}

void WebSocketHandler::assignRoleOnConnect(uint32_t clientId) {
    if (controllerClientId == 0) {
        if (controllerRoleLocked && controllerOwnerKey.length() > 0) {
            addViewer(clientId);
            Serial.printf("[WebSocketHandler] Client %u assigned VIEWER role (controller lock reserved)\n", clientId);
            return;
        }

        controllerClientId = clientId;
        Serial.printf("[WebSocketHandler] Client %u assigned CONTROLLER role\n", clientId);
        return;
    }

    addViewer(clientId);
    Serial.printf("[WebSocketHandler] Client %u assigned VIEWER role\n", clientId);
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

    server.onNotFound([](AsyncWebServerRequest* request) {
        String path = request->url();
        if (request->method() != HTTP_GET) {
            request->send(404);
            return;
        }

        if (path == "/ws" || path.startsWith("/ws/")) {
            request->send(404);
            return;
        }

        if (path.indexOf('.') >= 0) {
            request->send(404);
            return;
        }

        request->send(LittleFS, "/index.html", "text/html");
    });
    
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

    if (relayTestActive && !relayManager.isZoneFiring()) {
        uint32_t elapsedMs = now - relayTestStepStartMs;
        if (elapsedMs >= relayTestPulseMs + 100) {
            advanceRelayTest();
        }
    }

}

void WebSocketHandler::onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        uint32_t clientId = client->id();
        Serial.printf("[WebSocketHandler] Client connected: %u\n", clientId);

        wsHandler.assignRoleOnConnect(clientId);

        // Send authoritative initial state directly to this client.
        wsHandler.broadcastFullState(clientId);
        wsHandler.sendRoleToClient(clientId);

    } else if (type == WS_EVT_DISCONNECT) {
        uint32_t clientId = client->id();
        Serial.printf("[WebSocketHandler] Client disconnected: %u\n", clientId);
        
        wsHandler.removeClient(clientId);
        
        if (wsHandler.controllerClientId == clientId) {
            if (wsHandler.controllerRoleLocked) {
                String controllerKey = wsHandler.getClientIdentity(clientId);
                if (controllerKey.length() > 0) {
                    wsHandler.controllerOwnerKey = controllerKey;
                }
            }

            wsHandler.controllerClientId = 0;
            if (!wsHandler.controllerRoleLocked) {
                wsHandler.promoteViewerToController();
            } else {
                Serial.println("[WebSocketHandler] Controller disconnected while role lock is enabled; waiting for owner reconnect");
            }
            wsHandler.broadcastRoleAssignment();
            wsHandler.broadcastFullState();
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
            } else if (strcmp(msgType, "set_ap_credentials") == 0) {
                wsHandler.handleApConfigCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "wifi_scan") == 0) {
            } else if (strcmp(msgType, "wifi_connect") == 0) {
                wsHandler.handleWiFiConnectCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "wifi_forget") == 0) {
                wsHandler.handleForgetWiFiCommand(clientId);
            } else if (strcmp(msgType, "get_role") == 0) {
                wsHandler.sendRoleToClient(clientId);
            } else if (strcmp(msgType, "get_state") == 0) {
                wsHandler.broadcastFullState(clientId);
            } else if (strcmp(msgType, "import_show") == 0) {
                wsHandler.handleImportShowCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "relay_test") == 0) {
                wsHandler.handleRelayTestCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "client_hello") == 0) {
                wsHandler.handleClientHello(clientId, (const char*)data);
            } else if (strcmp(msgType, "set_role_lock") == 0) {
                wsHandler.handleRoleLockCommand(clientId, (const char*)data);
            } else if (strcmp(msgType, "pong") == 0) {
                // Heartbeat response
            }
        }
    }
}

void WebSocketHandler::removeClient(uint32_t clientId) {
    removeViewer(clientId);

    for (auto it = clientIdentities.begin(); it != clientIdentities.end(); ++it) {
        if (it->clientId == clientId) {
            clientIdentities.erase(it);
            break;
        }
    }
}

void WebSocketHandler::promoteViewerToController() {
    if (!viewerClientIds.empty()) {
        controllerClientId = viewerClientIds.front();
        viewerClientIds.erase(viewerClientIds.begin());
        if (controllerRoleLocked) {
            String controllerKey = getClientIdentity(controllerClientId);
            if (controllerKey.length() > 0) {
                controllerOwnerKey = controllerKey;
            }
        }
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

void WebSocketHandler::broadcastFullState(uint32_t targetClientId) {
    DynamicJsonDocument doc(12288);

    doc["type"] = "full_state";
    doc["masterArmed"] = relayManager.isMasterArmed();
    doc["boardCount"] = relayManager.boardPresentCount;
    doc["detectedBoards"] = relayManager.boardPresentCount;
    doc["roleLocked"] = controllerRoleLocked;

    if (targetClientId != 0) {
        doc["role"] = (targetClientId == controllerClientId) ? "controller" : "viewer";
    }

    JsonObject wifi = doc.createNestedObject("wifiConfig");
    wifi["apSsid"] = storage.getApSSID();
    wifi["apPassword"] = storage.getApPassword();
    wifi["clientSsid"] = storage.getClientSSID();
    wifi["clientPassword"] = storage.getClientPassword();
    wifi["connected"] = wifiManager.isClientConnected();
    wifi["currentSsid"] = wifiManager.getCurrentSSID();
    wifi["apActive"] = wifiManager.isAPActive();
    wifi["connecting"] = wifiManager.isConnecting();

    JsonObject settings = doc.createNestedObject("settings");
    settings["igniterDurationMs"] = storage.getIgniterDuration();
    settings["igniterDurationSec"] = storage.getIgniterDuration() / 1000.0f;
    settings["autoDelay"] = storage.getAutoDelay();
    settings["abortOnDisconnect"] = storage.getAbortOnDisconnect();
    settings["eStopResetMode"] = static_cast<uint8_t>(storage.getEStopResetMode());
    settings["continuityLoGood"] = storage.getContinuityLoGood();
    settings["continuityHiGood"] = storage.getContinuityHiGood();
    settings["continuityLoOpen"] = storage.getContinuityLoOpen();

    JsonArray auxNames = doc.createNestedArray("auxNames");
    auxNames.add(storage.getAuxRelayName(0));
    auxNames.add(storage.getAuxRelayName(1));

    JsonArray groups = doc.createNestedArray("groups");
    for (uint8_t groupIdx = 0; groupIdx < MAX_GROUPS; groupIdx++) {
        String name = storage.getGroupName(groupIdx);
        String members = storage.getGroupMembers(groupIdx);
        if (name.length() == 0 && members.length() == 0) {
            continue;
        }

        JsonObject group = groups.createNestedObject();
        group["id"] = groupIdx;
        group["name"] = name;
        group["members"] = members;
        group["order"] = storage.getGroupOrder(groupIdx);
    }

    JsonArray zones = doc.createNestedArray("zones");
    for (uint8_t zoneIdx = 0; zoneIdx < MAX_ZONES; zoneIdx++) {
        JsonObject zone = zones.createNestedObject();
        zone["id"] = zoneIdx;
        zone["description"] = storage.getZoneDescription(zoneIdx);
        zone["time"] = storage.getZoneTime(zoneIdx);
        zone["enabled"] = storage.isZoneEnabled(zoneIdx);
        zone["group"] = storage.getZoneGroup(zoneIdx);
        zone["order"] = storage.getZoneOrder(zoneIdx);
        zone["continuity"] = static_cast<uint8_t>(continuityManager.getZoneStatus(zoneIdx));
    }

    String json;
    serializeJson(doc, json);

    if (targetClientId == 0) {
        ws.textAll(json);
        return;
    }

    AsyncWebSocketClient* targetClient = ws.client(targetClientId);
    if (targetClient) {
        targetClient->text(json);
    }
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

void WebSocketHandler::sendRoleToClient(uint32_t clientId) {
    if (clientId == 0) {
        return;
    }

    StaticJsonDocument<128> doc;
    doc["type"] = "role";
    doc["role"] = (clientId == controllerClientId) ? "controller" : "viewer";
    doc["roleLocked"] = controllerRoleLocked;

    String json;
    serializeJson(doc, json);

    AsyncWebSocketClient* client = ws.client(clientId);
    if (client) {
        client->text(json);
    }
}

void WebSocketHandler::broadcastRoleAssignment() {
    if (controllerClientId != 0) {
        sendRoleToClient(controllerClientId);
    }

    for (uint32_t clientId : viewerClientIds) {
        sendRoleToClient(clientId);
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
    broadcastFullState();
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
    stopRelayTest(true);
    showRunner.abortShow();
    relayManager.setMasterArm(false);
    relayManager.setAllRelaysOff();
    broadcastEStop();
    broadcastSystemStatus();
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
        broadcastFullState();
    }
}

void WebSocketHandler::handleGroupConfigCommand(uint32_t clientId, const char* data) {
    StaticJsonDocument<512> doc;
    deserializeJson(doc, data);
    
    uint8_t groupId = doc["id"] | 0;
    if (doc.containsKey("name")) storage.setGroupName(groupId, doc["name"].as<String>());
    if (doc.containsKey("members")) storage.setGroupMembers(groupId, doc["members"].as<String>());
    if (doc.containsKey("order")) storage.setGroupOrder(groupId, doc["order"]);
    broadcastFullState();
}

void WebSocketHandler::handleSettingCommand(uint32_t clientId, const char* data) {
    // Save generic settings
    StaticJsonDocument<512> doc;
    deserializeJson(doc, data);
    
    const char* key = doc["key"] | "";
    const char* value = doc["value"] | "";

    if (strcmp(key, NVS_KEYS::SETTING_IGNITER_DURATION) == 0) {
        storage.setIgniterDuration(static_cast<uint16_t>(String(value).toInt()));
    } else if (strcmp(key, NVS_KEYS::SETTING_AUTO_DELAY) == 0) {
        storage.setAutoDelay(static_cast<uint8_t>(String(value).toInt()));
    } else if (strcmp(key, NVS_KEYS::SETTING_ABORT_ON_DISCONNECT) == 0) {
        storage.setAbortOnDisconnect(String(value) == "true" || String(value) == "1");
    } else if (strcmp(key, NVS_KEYS::SETTING_ESTOP_RESET_MODE) == 0) {
        storage.setEStopResetMode(static_cast<EStopResetMode>(String(value).toInt()));
    } else if (strcmp(key, NVS_KEYS::SETTING_BOARD_COUNT) == 0) {
        storage.setBoardCount(static_cast<uint8_t>(String(value).toInt()));
    } else if (strcmp(key, NVS_KEYS::SETTING_CONTINUITY_LO_GOOD) == 0) {
        storage.setContinuityThresholds(String(value).toFloat(), storage.getContinuityHiGood(), storage.getContinuityLoOpen());
    } else if (strcmp(key, NVS_KEYS::SETTING_CONTINUITY_HI_GOOD) == 0) {
        storage.setContinuityThresholds(storage.getContinuityLoGood(), String(value).toFloat(), storage.getContinuityLoOpen());
    } else if (strcmp(key, NVS_KEYS::SETTING_CONTINUITY_LO_OPEN) == 0) {
        storage.setContinuityThresholds(storage.getContinuityLoGood(), storage.getContinuityHiGood(), String(value).toFloat());
    } else {
        storage.saveSetting(key, String(value));
    }

    broadcastFullState();
}

void WebSocketHandler::handleAuxNameCommand(uint32_t clientId, const char* data) {
    StaticJsonDocument<256> doc;
    deserializeJson(doc, data);
    
    uint8_t relay = doc["relay"] | 0;
    const char* name = doc["name"] | "";

    storage.setAuxRelayName(relay, name);
    broadcastFullState();
}

void WebSocketHandler::handleApConfigCommand(uint32_t clientId, const char* data) {
    StaticJsonDocument<256> doc;
    deserializeJson(doc, data);

    const char* ssid = doc["ssid"] | DEFAULT_AP_SSID;
    const char* pass = doc["pass"] | DEFAULT_AP_PASSWORD;

    storage.setApCredentials(ssid, pass);
    broadcastFullState();
}

void WebSocketHandler::handleForgetWiFiCommand(uint32_t clientId) {
    wifiManager.forgetNetwork();
    broadcastWiFiStatus();
    broadcastFullState();
}

void WebSocketHandler::handleWiFiConnectCommand(uint32_t clientId, const char* data) {
    StaticJsonDocument<256> doc;
    deserializeJson(doc, data);
    
    const char* ssid = doc["ssid"] | "";
    const char* pass = doc["pass"] | "";

    if (strlen(ssid) == 0) {
        broadcastError("INVALID_SSID", "SSID cannot be empty");
        return;
    }

    Serial.printf("[WiFi] WiFi connect command received: ssid=%s\n", ssid);
    wifiManager.scheduleConnect(ssid, pass);
    delay(50);
    broadcastFullState();  // Broadcast after connection attempt has begun
}

void WebSocketHandler::handleRelayTestCommand(uint32_t clientId, const char* data) {
    if (!relayManager.isMasterArmed()) {
        broadcastError("NOT_ARMED", "System must be armed for relay test");
        return;
    }

    if (relayTestActive) {
        broadcastError("TEST_RUNNING", "Relay test is already running");
        return;
    }

    if (showRunner.isShowRunning()) {
        broadcastError("SHOW_RUNNING", "Stop the auto show before running a relay test");
        return;
    }

    startRelayTest();
}

void WebSocketHandler::handleClientHello(uint32_t clientId, const char* data) {
    StaticJsonDocument<256> doc;
    deserializeJson(doc, data);

    String key = doc["clientKey"] | "";
    key.trim();
    if (key.length() == 0) {
        return;
    }

    setClientIdentity(clientId, key);

    if (controllerClientId == clientId && controllerRoleLocked) {
        controllerOwnerKey = key;
    }

    if (controllerRoleLocked && controllerOwnerKey.length() > 0 && controllerOwnerKey == key) {
        if (controllerClientId != clientId) {
            if (controllerClientId != 0) {
                addViewer(controllerClientId);
            }
            removeViewer(clientId);
            controllerClientId = clientId;
            Serial.printf("[WebSocketHandler] Client %u reclaimed CONTROLLER role via lock\n", clientId);
            broadcastRoleAssignment();
            broadcastFullState();
            return;
        }
    }

    if (controllerClientId == 0 && !controllerRoleLocked) {
        removeViewer(clientId);
        controllerClientId = clientId;
        Serial.printf("[WebSocketHandler] Client %u became CONTROLLER (lock disabled)\n", clientId);
        broadcastRoleAssignment();
        broadcastFullState();
        return;
    }

    sendRoleToClient(clientId);
    broadcastFullState(clientId);
}

void WebSocketHandler::handleRoleLockCommand(uint32_t clientId, const char* data) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can change role lock");
        return;
    }

    StaticJsonDocument<128> doc;
    deserializeJson(doc, data);
    bool locked = doc["locked"] | true;

    controllerRoleLocked = locked;
    if (controllerRoleLocked) {
        String controllerKey = getClientIdentity(controllerClientId);
        if (controllerKey.length() > 0) {
            controllerOwnerKey = controllerKey;
        }
    }

    Serial.printf("[WebSocketHandler] Controller role lock set to %s\n", controllerRoleLocked ? "LOCKED" : "UNLOCKED");
    broadcastRoleAssignment();
    broadcastFullState();
}

void WebSocketHandler::startRelayTest() {
    relayTestZones.clear();
    relayTestStepIdx = 0;
    relayTestPulseMs = storage.getIgniterDuration();

    for (uint8_t boardIdx = 0; boardIdx < MAX_BOARDS; boardIdx++) {
        if (!relayManager.boardPresent[boardIdx]) {
            continue;
        }

        for (uint8_t relayIdx = 0; relayIdx < RELAYS_PER_BOARD; relayIdx++) {
            relayTestZones.push_back((boardIdx * RELAYS_PER_BOARD) + relayIdx);
        }
    }

    if (relayTestZones.empty()) {
        broadcastError("NO_RELAYS", "No relay boards detected for test");
        return;
    }

    relayTestActive = true;
    relayTestStepStartMs = millis();
    relayManager.startZoneFire(relayTestZones[relayTestStepIdx], relayTestPulseMs);
    broadcastShowProgress(1, relayTestZones.size(), relayTestZones[relayTestStepIdx]);
    broadcastSystemStatus();
}

void WebSocketHandler::advanceRelayTest() {
    if (!relayTestActive) {
        return;
    }

    relayTestStepIdx++;
    if (relayTestStepIdx >= relayTestZones.size()) {
        stopRelayTest(false);
        return;
    }

    relayTestStepStartMs = millis();
    relayManager.startZoneFire(relayTestZones[relayTestStepIdx], relayTestPulseMs);
    broadcastShowProgress(relayTestStepIdx + 1, relayTestZones.size(), relayTestZones[relayTestStepIdx]);
}

void WebSocketHandler::handleImportShowCommand(uint32_t clientId, const char* data) {
    StaticJsonDocument<8192> doc;
    deserializeJson(doc, data);
    
    if (doc.containsKey("data")) {
        String jsonData;
        serializeJson(doc["data"], jsonData);
        storage.importShowJson(jsonData);
        broadcastFullState();
    }
}

void WebSocketHandler::stopRelayTest(bool aborted) {
    if (!relayTestActive) {
        return;
    }

    relayManager.stopZoneFire();
    relayManager.setAllRelaysOff();
    relayTestActive = false;
    relayTestZones.clear();
    relayTestStepIdx = 0;
    relayTestStepStartMs = 0;
    relayTestPulseMs = 0;

    if (aborted) {
        broadcastError("TEST_ABORTED", "Relay test aborted");
    } else {
        broadcastShowProgress(0, 0, 0);
    }
}
