#ifndef WEBSOCKET_HANDLER_H
#define WEBSOCKET_HANDLER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "config.h"

class WebSocketHandler {
public:
    WebSocketHandler();
    
    void begin();
    void update();
    
    // Broadcasting state to connected clients
    void broadcastFullState();
    void broadcastContinuity();
    void broadcastBatteryStatus();
    void broadcastWiFiStatus();
    void broadcastZoneFired(uint8_t zoneIdx, uint32_t duration);
    void broadcastShowProgress(uint8_t currentStep, uint8_t totalSteps, uint8_t currentZone);
    void broadcastEStop();
    void broadcastError(const char* code, const char* message);
    void broadcastRoleAssignment();
    void broadcastSystemStatus();
    
private:
    AsyncWebServer server;
    AsyncWebSocket ws;
    
    uint32_t controllerClientId;  // 0 = no controller
    uint32_t lastHeartbeatMs;
    std::vector<uint32_t> viewerClientIds;
    
    // Heartbeat & timeout
    void handleHeartbeatTimeout();
    void sendHeartbeat();
    bool validateChecksum(const char* command, uint8_t zone, uint32_t timestamp, const char* checksum);
    
    // Message handlers
    void handleFireCommand(uint32_t clientId, const char* data);
    void handleArmCommand(uint32_t clientId, const char* data);
    void handleAuxCommand(uint32_t clientId, const char* data);
    void handleEStopCommand(uint32_t clientId);
    void handleEStopReset(uint32_t clientId);
    void handleAutoStartCommand(uint32_t clientId, const char* data);
    void handleAutoStopCommand(uint32_t clientId);
    void handleZoneConfigCommand(uint32_t clientId, const char* data);
    void handleGroupConfigCommand(uint32_t clientId, const char* data);
    void handleSettingCommand(uint32_t clientId, const char* data);
    void handleAuxNameCommand(uint32_t clientId, const char* data);
    void handleWiFiScanCommand(uint32_t clientId);
    void handleWiFiConnectCommand(uint32_t clientId, const char* data);
    void handleImportShowCommand(uint32_t clientId, const char* data);
    void handleRelayTestCommand(uint32_t clientId, const char* data);
    
    // WebSocket callbacks
    static void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                          AwsEventType type, void* arg, uint8_t* data, size_t len);
    
    // Client management
    void promoteViewerToController();
    void removeClient(uint32_t clientId);
    
    // Utility
    uint32_t calculateCrc32(const String& data);
    String formatContinuityArray();
};

extern WebSocketHandler wsHandler;

#endif  // WEBSOCKET_HANDLER_H
