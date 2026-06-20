#include "websocket_handler.h"
#include "storage.h"
#include "wifi_manager.h"
#include "relay_manager.h"
#include "continuity.h"
#include "show_runner.h"
#include <LittleFS.h>

namespace {
const uint32_t FULL_STATE_BROADCAST_MIN_INTERVAL_MS = 300;
const uint32_t ROLE_LOCK_RECLAIM_TIMEOUT_MS = 15000;
}

WebSocketHandler wsHandler;

// WebSocketHandler implementation starts here


WebSocketHandler::WebSocketHandler()
    : server(80), ws("/ws"), controllerClientId(0), controllerRoleLocked(true), controllerOwnerKey(""),
    lastHeartbeatMs(0), lastControllerMessageMs(0), lastControllerPongMs(0), lastShowStateBroadcastMs(0),
    lastFullStateBroadcastMs(0), controllerVacantSinceMs(0), estopLatched(false), estopResetPending(false),
    fullStateDirty(false), relayTestActive(false), relayTestStepIdx(0), relayTestStepStartMs(0), relayTestPulseMs(0) {
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
    
    uint32_t now = millis();
    lastHeartbeatMs = now;
    lastControllerMessageMs = now;
    lastControllerPongMs = now;
    lastShowStateBroadcastMs = now;
    lastFullStateBroadcastMs = now;
}

void WebSocketHandler::update() {
    uint32_t now = millis();

    // Keep async websocket client list healthy under reconnect churn.
    ws.cleanupClients();
    
    // Send heartbeat every 5 seconds
    if (now - lastHeartbeatMs >= WEBSOCKET_HEARTBEAT_INTERVAL_MS) {
        sendHeartbeat();
        lastHeartbeatMs = now;
    }

    handleHeartbeatTimeout();
    
    // Broadcast periodic status
    static uint32_t lastBroadcast = 0;
    if (now - lastBroadcast >= 1000) {
        broadcastContinuity();
        broadcastBatteryStatus();
        broadcastWiFiStatus();
        lastBroadcast = now;
    }

    if (now - lastShowStateBroadcastMs >= 500) {
        broadcastShowState();
        lastShowStateBroadcastMs = now;
    }

    if (fullStateDirty && now - lastFullStateBroadcastMs >= FULL_STATE_BROADCAST_MIN_INTERVAL_MS) {
        broadcastFullState();
        lastFullStateBroadcastMs = now;
        fullStateDirty = false;
    }

    // Broadcast lightweight system_status once after each relay auto-off so UI
    // fire buttons and arm state are updated promptly without a full-state flood.
    if (relayManager.consumeFireComplete()) {
        broadcastSystemStatus();
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
        if (wsHandler.controllerClientId == clientId) {
            uint32_t now = millis();
            wsHandler.lastControllerMessageMs = now;
            wsHandler.lastControllerPongMs = now;
        }

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

            if (showRunner.isShowRunning() && storage.getAbortOnDisconnect()) {
                Serial.println("[WebSocketHandler] Controller disconnected while auto show running; aborting by policy");
                wsHandler.triggerEmergencyStop("Controller disconnected");
            } else if (!showRunner.isShowRunning()) {
                relayManager.setMasterArm(false);
                relayManager.setAllRelaysOff();
            }

            wsHandler.controllerClientId = 0;
            wsHandler.controllerVacantSinceMs = millis();
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
            String payload;
            payload.reserve(len + 1);
            for (size_t i = 0; i < len; i++) {
                payload += static_cast<char>(data[i]);
            }

            StaticJsonDocument<32> typeFilter;
            typeFilter["type"] = true;
            StaticJsonDocument<96> doc;
            DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(typeFilter));
            if (error) {
                Serial.printf("[WebSocketHandler] JSON parse error: %s\n", error.c_str());
                return;
            }

            const char* msgType = doc["type"] | "";

            if (clientId == wsHandler.controllerClientId) {
                wsHandler.lastControllerMessageMs = millis();
            }

            if (strcmp(msgType, "fire") == 0) {
                wsHandler.handleFireCommand(clientId, payload.c_str());
            } else if (strcmp(msgType, "fire_group") == 0) {
                wsHandler.handleFireGroupCommand(clientId, payload.c_str());
            } else if (strcmp(msgType, "arm") == 0) {
                wsHandler.handleArmCommand(clientId, payload.c_str());
            } else if (strcmp(msgType, "aux") == 0) {
                wsHandler.handleAuxCommand(clientId, payload.c_str());
            } else if (strcmp(msgType, "estop") == 0) {
                wsHandler.handleEStopCommand(clientId);
            } else if (strcmp(msgType, "estop_reset") == 0) {
                wsHandler.handleEStopReset(clientId);
            } else if (strcmp(msgType, "auto_start") == 0) {
                wsHandler.handleAutoStartCommand(clientId, payload.c_str());
            } else if (strcmp(msgType, "auto_stop") == 0) {
                wsHandler.handleAutoStopCommand(clientId);
            } else if (strcmp(msgType, "set_zone") == 0) {
                wsHandler.handleZoneConfigCommand(clientId, payload.c_str());
            } else if (strcmp(msgType, "set_setting") == 0) {
                wsHandler.handleSettingCommand(clientId, payload.c_str());
            } else if (strcmp(msgType, "save_builder") == 0) {
                wsHandler.handleBuilderSaveCommand(clientId, payload.c_str());
            } else if (strcmp(msgType, "set_aux_name") == 0) {
                wsHandler.handleAuxNameCommand(clientId, payload.c_str());
            } else if (strcmp(msgType, "set_ap_credentials") == 0) {
                wsHandler.handleApConfigCommand(clientId, payload.c_str());
            } else if (strcmp(msgType, "wifi_scan") == 0) {
                wsHandler.handleWiFiScanCommand(clientId);
            } else if (strcmp(msgType, "wifi_connect") == 0) {
                wsHandler.handleWiFiConnectCommand(clientId, payload.c_str());
            } else if (strcmp(msgType, "wifi_forget") == 0) {
                wsHandler.handleForgetWiFiCommand(clientId);
            } else if (strcmp(msgType, "clear_all") == 0) {
                wsHandler.handleClearAllCommand(clientId);
            } else if (strcmp(msgType, "export_show") == 0) {
                wsHandler.handleExportShowCommand(clientId);
            } else if (strcmp(msgType, "get_role") == 0) {
                wsHandler.sendRoleToClient(clientId);
            } else if (strcmp(msgType, "get_state") == 0) {
                wsHandler.broadcastFullState(clientId);
            } else if (strcmp(msgType, "import_show") == 0) {
                wsHandler.handleImportShowCommand(clientId, payload.c_str());
            } else if (strcmp(msgType, "relay_test") == 0) {
                wsHandler.handleRelayTestCommand(clientId, payload.c_str());
            } else if (strcmp(msgType, "client_hello") == 0) {
                wsHandler.handleClientHello(clientId, payload.c_str());
            } else if (strcmp(msgType, "set_role_lock") == 0) {
                wsHandler.handleRoleLockCommand(clientId, payload.c_str());
            } else if (strcmp(msgType, "pong") == 0) {
                if (clientId == wsHandler.controllerClientId) {
                    wsHandler.lastControllerPongMs = millis();
                }
            }
        }
    }
}

void WebSocketHandler::markStateDirty() {
    fullStateDirty = true;
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
    doc["estopActive"] = estopLatched;
    doc["estopResetPending"] = estopResetPending;
    doc["showState"] = static_cast<uint8_t>(showRunner.getShowState());
    doc["showRunning"] = showRunner.isShowRunning();

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

    JsonArray auxState = doc.createNestedArray("auxState");
    auxState.add(relayManager.getAuxRelayState(0));
    auxState.add(relayManager.getAuxRelayState(1));

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
    doc["rssi"] = wifiManager.getRSSI();
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
    JsonArray auxState = doc.createNestedArray("auxState");
    auxState.add(relayManager.getAuxRelayState(0));
    auxState.add(relayManager.getAuxRelayState(1));
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void WebSocketHandler::broadcastShowState() {
    StaticJsonDocument<192> doc;
    doc["type"] = "show_state";
    doc["state"] = static_cast<uint8_t>(showRunner.getShowState());
    doc["running"] = showRunner.isShowRunning();
    doc["step"] = showRunner.getCurrentStepIdx();
    doc["total"] = showRunner.getTotalSteps();

    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void WebSocketHandler::handleHeartbeatTimeout() {
    if (controllerClientId == 0) {
        return;
    }

    uint32_t now = millis();
    uint32_t lastActivityMs = lastControllerPongMs;
    if (lastControllerMessageMs > lastActivityMs) {
        lastActivityMs = lastControllerMessageMs;
    }

    if (now - lastActivityMs < WEBSOCKET_HEARTBEAT_TIMEOUT_MS) {
        return;
    }

    Serial.println("[WebSocketHandler] Controller heartbeat timeout");
    if (showRunner.isShowRunning()) {
        if (storage.getAbortOnDisconnect()) {
            triggerEmergencyStop("Controller heartbeat timeout");
        }
    } else {
        relayManager.setMasterArm(false);
        relayManager.setAllRelaysOff();
        broadcastSystemStatus();
        markStateDirty();
    }

    lastControllerPongMs = now;
    lastControllerMessageMs = now;
}

void WebSocketHandler::triggerEmergencyStop(const char* reason) {
    (void)reason;
    stopRelayTest(true);
    showRunner.abortShow();
    relayManager.setMasterArm(false);
    relayManager.setAllRelaysOff();
    estopLatched = true;
    estopResetPending = false;
    broadcastEStop();
    broadcastSystemStatus();
    markStateDirty();
}

// ============================================================================
// Message Handlers (Stubs for now - will be filled in fully)
// ============================================================================

void WebSocketHandler::handleFireCommand(uint32_t clientId, const char* data) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can fire");
        return;
    }

    if (estopLatched) {
        broadcastError("ESTOP_ACTIVE", "Reset E-Stop before firing");
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

    if (zone >= MAX_ZONES) {
        broadcastError("INVALID_ZONE", "Zone out of range");
        return;
    }

    if (!relayManager.isMasterArmed()) {
        broadcastError("NOT_ARMED", "Master arm is required before firing");
        return;
    }
    
    uint32_t runDurationMs = static_cast<uint32_t>(storage.getZoneTime(zone) * 1000.0f);
    if (runDurationMs == 0) {
        runDurationMs = 1;
    }

    uint16_t ignitersOnDuration = storage.getIgniterDuration();
    relayManager.startZoneFire(zone, ignitersOnDuration);
    broadcastZoneFired(zone, runDurationMs);
    broadcastSystemStatus();
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

    if (state && estopLatched) {
        broadcastError("ESTOP_ACTIVE", "Reset E-Stop before arming");
        return;
    }
    
    relayManager.setMasterArm(state);
    broadcastSystemStatus();
}

void WebSocketHandler::handleFireGroupCommand(uint32_t clientId, const char* data) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can fire");
        return;
    }

    if (estopLatched) {
        broadcastError("ESTOP_ACTIVE", "Reset E-Stop before firing");
        return;
    }

    StaticJsonDocument<256> doc;
    deserializeJson(doc, data);

    uint8_t groupId = doc["group"] | 0;
    uint32_t ts = doc["ts"] | 0;
    const char* cs = doc["cs"] | "";

    if (!validateChecksum("FIRE_GROUP", groupId, ts, cs)) {
        broadcastError("BAD_CHECKSUM", "Checksum validation failed");
        return;
    }

    if (!relayManager.isMasterArmed()) {
        broadcastError("NOT_ARMED", "Master arm is required before firing");
        return;
    }

    std::vector<uint8_t> zones;
    for (uint8_t zoneIdx = 0; zoneIdx < MAX_ZONES; zoneIdx++) {
        if (!storage.isZoneEnabled(zoneIdx)) {
            continue;
        }
        if (storage.getZoneGroup(zoneIdx) == groupId) {
            zones.push_back(zoneIdx);
        }
    }

    if (zones.empty()) {
        broadcastError("INVALID_GROUP", "Group has no enabled zones");
        return;
    }

    uint32_t runDurationMs = 0;
    for (uint8_t zoneIdx : zones) {
        uint32_t zoneDurationMs = static_cast<uint32_t>(storage.getZoneTime(zoneIdx) * 1000.0f);
        if (zoneDurationMs > runDurationMs) {
            runDurationMs = zoneDurationMs;
        }
    }
    if (runDurationMs == 0) {
        runDurationMs = 1;
    }

    uint16_t ignitersOnDuration = storage.getIgniterDuration();
    relayManager.startZonesFire(zones, ignitersOnDuration);
    for (uint8_t zoneIdx : zones) {
        broadcastZoneFired(zoneIdx, runDurationMs);
    }
    broadcastSystemStatus();
}

void WebSocketHandler::handleAuxCommand(uint32_t clientId, const char* data) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can toggle auxiliary relays");
        return;
    }

    StaticJsonDocument<256> doc;
    deserializeJson(doc, data);
    
    uint8_t relay = doc["relay"] | 0;
    bool state = doc["state"] | false;
    
    relayManager.setAuxRelay(relay, state);
    broadcastSystemStatus();
}

void WebSocketHandler::handleEStopCommand(uint32_t clientId) {
    // E-Stop is allowed from any client (emergency override)
    triggerEmergencyStop("E-Stop button");
}

void WebSocketHandler::handleEStopReset(uint32_t clientId) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can reset E-Stop");
        return;
    }

    if (!estopLatched) {
        broadcastSystemStatus();
        return;
    }

    EStopResetMode mode = storage.getEStopResetMode();
    if (mode == EStopResetMode::POWER_CYCLE_ONLY) {
        broadcastError("RESET_BLOCKED", "Power cycle is required to clear E-Stop");
        return;
    }

    if (mode == EStopResetMode::TWO_STEP_CONFIRM && !estopResetPending) {
        estopResetPending = true;
        broadcastError("CONFIRM_RESET", "Press reset again to clear E-Stop");
        return;
    }

    estopLatched = false;
    estopResetPending = false;
    broadcastSystemStatus();
}

void WebSocketHandler::handleAutoStartCommand(uint32_t clientId, const char* data) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can start auto show");
        return;
    }
    
    if (estopLatched) {
        broadcastError("ESTOP_ACTIVE", "Reset E-Stop before starting auto show");
        return;
    }

    StaticJsonDocument<256> doc;
    deserializeJson(doc, data);

    uint8_t zone = doc["zone"] | 0;
    uint32_t ts = doc["ts"] | 0;
    const char* cs = doc["cs"] | "";
    if (!validateChecksum("AUTO_START", zone, ts, cs)) {
        broadcastError("BAD_CHECKSUM", "Checksum validation failed");
        return;
    }

    if (!showRunner.startShow()) {
        broadcastError("AUTO_START_FAILED", "Unable to start auto show. Check arm, enabled zones, and timing.");
        return;
    }

    broadcastShowState();
    broadcastSystemStatus();
}

void WebSocketHandler::handleAutoStopCommand(uint32_t clientId) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can stop auto show");
        return;
    }

    showRunner.stopShow();
    broadcastShowState();
    broadcastSystemStatus();
}

void WebSocketHandler::handleZoneConfigCommand(uint32_t clientId, const char* data) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can update zones");
        return;
    }

    StaticJsonDocument<512> doc;
    deserializeJson(doc, data);

    uint8_t zone = doc["zone"] | 255;
    if (zone >= MAX_ZONES) {
        broadcastError("INVALID_ZONE", "Zone index out of range");
        return;
    }

    // Use batch write: one NVS open/close for all 5 fields.
    // Read current persisted values first so unchanged fields are not dirtied.
    String desc    = doc.containsKey("desc")    ? doc["desc"].as<String>()   : storage.getZoneDescription(zone);
    float  time    = doc.containsKey("time")    ? doc["time"].as<float>()    : storage.getZoneTime(zone);
    bool   enabled = doc.containsKey("enabled") ? doc["enabled"].as<bool>()  : storage.isZoneEnabled(zone);
    uint8_t grp    = doc.containsKey("group")   ? (uint8_t)doc["group"].as<int>() : storage.getZoneGroup(zone);
    uint8_t ord    = doc.containsKey("order")   ? (uint8_t)doc["order"].as<int>() : storage.getZoneOrder(zone);

    storage.setZoneBatch(zone, desc, time, enabled, grp, ord);
    Serial.printf("[Storage] Zone %u saved: desc='%s' time=%.2f grp=%u ord=%u enabled=%d\n",
                  zone, desc.c_str(), time, grp, ord, (int)enabled);
    markStateDirty();
}

void WebSocketHandler::handleSettingCommand(uint32_t clientId, const char* data) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can update settings");
        return;
    }

    // Save generic settings
    StaticJsonDocument<512> doc;
    deserializeJson(doc, data);
    
    const char* key = doc["key"] | "";
    JsonVariant value = doc["value"];

    auto valueAsInt = [&value]() -> int {
        if (value.is<const char*>()) {
            return String(value.as<const char*>()).toInt();
        }
        return value.as<int>();
    };

    auto valueAsFloat = [&value]() -> float {
        if (value.is<const char*>()) {
            return String(value.as<const char*>()).toFloat();
        }
        return value.as<float>();
    };

    auto valueAsBool = [&value]() -> bool {
        if (value.is<bool>()) {
            return value.as<bool>();
        }
        if (value.is<const char*>()) {
            String raw = value.as<const char*>();
            raw.toLowerCase();
            return raw == "true" || raw == "1";
        }
        return value.as<int>() != 0;
    };

    if (strcmp(key, NVS_KEYS::SETTING_IGNITER_DURATION) == 0) {
        int ms = valueAsInt();
        ms = constrain(ms, 100, 10000);
        storage.setIgniterDuration(static_cast<uint16_t>(ms));
    } else if (strcmp(key, NVS_KEYS::SETTING_AUTO_DELAY) == 0) {
        int delaySec = valueAsInt();
        delaySec = constrain(delaySec, 0, 60);
        storage.setAutoDelay(static_cast<uint8_t>(delaySec));
    } else if (strcmp(key, NVS_KEYS::SETTING_ABORT_ON_DISCONNECT) == 0) {
        storage.setAbortOnDisconnect(valueAsBool());
    } else if (strcmp(key, NVS_KEYS::SETTING_ESTOP_RESET_MODE) == 0) {
        storage.setEStopResetMode(static_cast<EStopResetMode>(valueAsInt()));
    } else if (strcmp(key, NVS_KEYS::SETTING_BOARD_COUNT) == 0) {
        storage.setBoardCount(static_cast<uint8_t>(valueAsInt()));
    } else if (strcmp(key, NVS_KEYS::SETTING_CONTINUITY_LO_GOOD) == 0) {
        storage.setContinuityThresholds(valueAsFloat(), storage.getContinuityHiGood(), storage.getContinuityLoOpen());
    } else if (strcmp(key, NVS_KEYS::SETTING_CONTINUITY_HI_GOOD) == 0) {
        storage.setContinuityThresholds(storage.getContinuityLoGood(), valueAsFloat(), storage.getContinuityLoOpen());
    } else if (strcmp(key, NVS_KEYS::SETTING_CONTINUITY_LO_OPEN) == 0) {
        storage.setContinuityThresholds(storage.getContinuityLoGood(), storage.getContinuityHiGood(), valueAsFloat());
    } else {
        if (value.is<const char*>()) {
            storage.saveSetting(key, String(value.as<const char*>()));
        } else if (value.is<bool>()) {
            storage.saveSetting(key, value.as<bool>());
        } else if (value.is<float>() || value.is<double>()) {
            storage.saveSetting(key, value.as<float>());
        } else {
            storage.saveSetting(key, valueAsInt());
        }
    }

    markStateDirty();
}

void WebSocketHandler::handleBuilderSaveCommand(uint32_t clientId, const char* data) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can save builder data");
        return;
    }

    DynamicJsonDocument doc(24576);
    DeserializationError error = deserializeJson(doc, data);
    if (error) {
        broadcastError("INVALID_JSON", "Builder save payload is invalid");
        return;
    }

    if (doc.containsKey("zones") && doc["zones"].is<JsonArray>()) {
        JsonArray zones = doc["zones"].as<JsonArray>();
        for (JsonObject zone : zones) {
            uint8_t zoneId = static_cast<uint8_t>(zone["id"] | 255);
            if (zoneId >= MAX_ZONES) {
                continue;
            }

            // Batch-write all provided fields in one NVS transaction.
            String desc    = zone.containsKey("description") ? zone["description"].as<String>() : storage.getZoneDescription(zoneId);
            float  time    = zone.containsKey("time")        ? zone["time"].as<float>()          : storage.getZoneTime(zoneId);
            bool   enabled = zone.containsKey("enabled")     ? zone["enabled"].as<bool>()        : storage.isZoneEnabled(zoneId);
            uint8_t grp    = zone.containsKey("group")       ? (uint8_t)zone["group"].as<int>()  : storage.getZoneGroup(zoneId);
            uint8_t ord    = zone.containsKey("order")       ? (uint8_t)zone["order"].as<int>()  : storage.getZoneOrder(zoneId);

            storage.setZoneBatch(zoneId, desc, time, enabled, grp, ord);
        }
    }

    if (doc.containsKey("auxNames") && doc["auxNames"].is<JsonArray>()) {
        JsonArray auxNames = doc["auxNames"].as<JsonArray>();
        if (auxNames.size() >= 2) {
            storage.setAuxRelayName(0, auxNames[0].as<String>());
            storage.setAuxRelayName(1, auxNames[1].as<String>());
        }
    }

    markStateDirty();
}

void WebSocketHandler::handleAuxNameCommand(uint32_t clientId, const char* data) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can rename auxiliary relays");
        return;
    }

    StaticJsonDocument<256> doc;
    deserializeJson(doc, data);
    
    uint8_t relay = doc["relay"] | 0;
    const char* name = doc["name"] | "";

    storage.setAuxRelayName(relay, name);
    markStateDirty();
}

void WebSocketHandler::handleApConfigCommand(uint32_t clientId, const char* data) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can update AP credentials");
        return;
    }

    StaticJsonDocument<256> doc;
    deserializeJson(doc, data);

    const char* ssid = doc["ssid"] | DEFAULT_AP_SSID;
    const char* pass = doc["pass"] | DEFAULT_AP_PASSWORD;

    storage.setApCredentials(ssid, pass);
    markStateDirty();
}

void WebSocketHandler::handleForgetWiFiCommand(uint32_t clientId) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can forget WiFi");
        return;
    }

    wifiManager.forgetNetwork();
    broadcastWiFiStatus();
    markStateDirty();
}

void WebSocketHandler::handleWiFiScanCommand(uint32_t clientId) {
    (void)clientId;
    int count = WiFi.scanNetworks(false, true);

    DynamicJsonDocument doc(4096);
    doc["type"] = "wifi_scan_results";
    JsonArray networks = doc.createNestedArray("networks");

    for (int i = 0; i < count; i++) {
        JsonObject entry = networks.createNestedObject();
        entry["ssid"] = WiFi.SSID(i);
        entry["rssi"] = WiFi.RSSI(i);
        entry["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    }

    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void WebSocketHandler::handleWiFiConnectCommand(uint32_t clientId, const char* data) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can connect WiFi");
        return;
    }

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
    markStateDirty();  // Queue full state after connection attempt has begun
}

void WebSocketHandler::handleRelayTestCommand(uint32_t clientId, const char* data) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can run relay test");
        return;
    }

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
            controllerVacantSinceMs = 0;
            Serial.printf("[WebSocketHandler] Client %u reclaimed CONTROLLER role via lock\n", clientId);
            broadcastRoleAssignment();
            broadcastFullState();
            return;
        }
    }

    if (controllerRoleLocked && controllerClientId == 0 && controllerOwnerKey.length() > 0 && controllerOwnerKey != key) {
        uint32_t now = millis();
        if (controllerVacantSinceMs != 0 && now - controllerVacantSinceMs >= ROLE_LOCK_RECLAIM_TIMEOUT_MS) {
            removeViewer(clientId);
            controllerClientId = clientId;
            controllerOwnerKey = key;
            controllerVacantSinceMs = 0;
            Serial.printf("[WebSocketHandler] Controller lock timeout elapsed, client %u claimed CONTROLLER role\n", clientId);
            broadcastRoleAssignment();
            broadcastFullState();
            return;
        }
    }

    if (controllerClientId == 0 && !controllerRoleLocked) {
        removeViewer(clientId);
        controllerClientId = clientId;
        controllerVacantSinceMs = 0;
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
    } else {
        controllerVacantSinceMs = 0;
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
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can import show data");
        return;
    }

    StaticJsonDocument<8192> doc;
    DeserializationError error = deserializeJson(doc, data);
    if (error) {
        broadcastError("INVALID_JSON", "Import payload is not valid JSON");
        return;
    }
    
    if (doc.containsKey("data")) {
        String jsonData;
        serializeJson(doc["data"], jsonData);
        if (!storage.importShowJson(jsonData)) {
            broadcastError("IMPORT_FAILED", "Unable to import show data");
            return;
        }
        broadcastFullState();
        return;
    }

    broadcastError("INVALID_IMPORT", "Missing import data payload");
}

void WebSocketHandler::handleExportShowCommand(uint32_t clientId) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can export show data");
        return;
    }

    String jsonData = storage.exportShowJson();
    DynamicJsonDocument doc(8448);
    doc["type"] = "export_show";
    doc["dataRaw"] = jsonData;

    String payload;
    serializeJson(doc, payload);
    AsyncWebSocketClient* targetClient = ws.client(clientId);
    if (targetClient) {
        targetClient->text(payload);
    }
}

void WebSocketHandler::handleClearAllCommand(uint32_t clientId) {
    if (clientId != controllerClientId) {
        broadcastError("UNAUTHORIZED", "Only controller can clear show data");
        return;
    }

    storage.clearAllZones();

    broadcastFullState();
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
