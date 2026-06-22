#ifndef WEBSOCKET_HANDLER_H
#define WEBSOCKET_HANDLER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <vector>
#include "config.h"

struct SerialMonitorClientState {
    uint32_t clientId;
    uint32_t lastSequence;
};

class WebSocketHandler {
public:
    WebSocketHandler();
    
    void begin();
    void update();
    
    // Broadcasting state to connected clients
    void broadcastFullState(uint32_t targetClientId = 0);
    void broadcastContinuity();
    void broadcastBatteryStatus();
    void broadcastWiFiStatus();
    void broadcastZoneFired(uint8_t zoneIdx, uint32_t progressDuration, uint32_t igniterDuration);
    void broadcastShowProgress(uint8_t currentStep, uint8_t totalSteps, uint8_t currentZone);
    void broadcastEStop();
    void broadcastError(const char* code, const char* message);
    void broadcastRoleAssignment();
    void broadcastSystemStatus();
    void broadcastShowState();
    void broadcastSerialMonitorState(uint32_t targetClientId = 0);
    void triggerEmergencyStop(const char* reason = "E-Stop");
    
private:
    struct ClientIdentity {
        uint32_t clientId;
        String key;
    };

    struct ClientPayloadBuffer {
        uint32_t clientId;
        String payload;
    };

    struct ClientCommandState {
        uint32_t clientId;
        uint64_t lastTimestampMs;
    };

    struct CommandNonce {
        uint32_t clientId;
        String nonce;
    };

    AsyncWebServer server;
    AsyncWebSocket ws;
    
    uint32_t controllerClientId;  // 0 = no controller
    bool controllerRoleLocked;
    String controllerOwnerKey;
    uint32_t lastHeartbeatMs;
    uint32_t lastControllerMessageMs;
    uint32_t lastControllerPongMs;
    uint32_t lastShowStateBroadcastMs;
    uint32_t lastFullStateBroadcastMs;
    uint32_t controllerVacantSinceMs;
    bool estopLatched;
    bool estopResetPending;
    bool fullStateDirty;
    std::vector<uint32_t> viewerClientIds;
    std::vector<ClientIdentity> clientIdentities;
    std::vector<ClientPayloadBuffer> payloadBuffers;
    std::vector<ClientCommandState> commandStates;
    std::vector<CommandNonce> recentCommandNonces;
    
    // Heartbeat & timeout
    void handleHeartbeatTimeout();
    void sendHeartbeat();
    void markStateDirty();
    bool parseJsonPayload(JsonDocument& doc, const char* data, const char* errorMessage);
    bool validateCommandSignature(uint32_t clientId, const char* command, uint8_t value, uint64_t timestampMs, const char* nonce, const char* signature);
    
    // Message handlers
    void handleFireCommand(uint32_t clientId, const char* data);
    void handleFireGroupCommand(uint32_t clientId, const char* data);
    void handleArmCommand(uint32_t clientId, const char* data);
    void handleExportShowCommand(uint32_t clientId);
    void handleExportSettingsCommand(uint32_t clientId);
    void handleClearAllCommand(uint32_t clientId);
    void handleAuxCommand(uint32_t clientId, const char* data);
    void handleEStopCommand(uint32_t clientId);
    void handleEStopReset(uint32_t clientId);
    void handleAutoStartCommand(uint32_t clientId, const char* data);
    void handleAutoStopCommand(uint32_t clientId);
    void handleZoneConfigCommand(uint32_t clientId, const char* data);
    void handleSettingCommand(uint32_t clientId, const char* data);
    void handleBuilderSaveCommand(uint32_t clientId, const char* data);
    void handleAuxNameCommand(uint32_t clientId, const char* data);
    void handleApConfigCommand(uint32_t clientId, const char* data);
    void handleForgetWiFiCommand(uint32_t clientId);
    void handleWiFiConnectCommand(uint32_t clientId, const char* data);
    void handleImportShowCommand(uint32_t clientId, const char* data);
    void handleImportSettingsCommand(uint32_t clientId, const char* data);
    void handleFactoryResetCommand(uint32_t clientId);
    void handleRelayTestCommand(uint32_t clientId, const char* data);
    void handleClientHello(uint32_t clientId, const char* data);
    void handleRoleLockCommand(uint32_t clientId, const char* data);
    void handleSerialMonitorCommand(uint32_t clientId, const char* data);
    
    // WebSocket callbacks
    static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                          AwsEventType type, void* arg, uint8_t* data, size_t len);
    
    // Client management
    void promoteViewerToController();
    void removeClient(uint32_t clientId);
    void sendRoleToClient(uint32_t clientId);
    void assignRoleOnConnect(uint32_t clientId);
    void setClientIdentity(uint32_t clientId, const String& key);
    String getClientIdentity(uint32_t clientId) const;
    bool hasViewer(uint32_t clientId) const;
    void addViewer(uint32_t clientId);
    void removeViewer(uint32_t clientId);
    String& getOrCreatePayloadBuffer(uint32_t clientId);
    void clearPayloadBuffer(uint32_t clientId);
    SerialMonitorClientState* findSerialMonitorClient(uint32_t clientId);
    void removeSerialMonitorClient(uint32_t clientId);
    bool isSerialMonitorRunning() const;
    void flushSerialMonitorLines();

    // Relay test state
    void startRelayTest();
    void advanceRelayTest();
    void stopRelayTest(bool aborted);
    
    // Utility
    String calculateHmacSha256(const String& key, const String& data);
    String formatContinuityArray();

    bool relayTestActive;
    std::vector<uint8_t> relayTestZones;
    size_t relayTestStepIdx;
    uint32_t relayTestStepStartMs;
    uint32_t relayTestPulseMs;
    uint32_t lastSerialMonitorFlushMs;
    std::vector<SerialMonitorClientState> serialMonitorClients;
};

extern WebSocketHandler wsHandler;

#endif  // WEBSOCKET_HANDLER_H
