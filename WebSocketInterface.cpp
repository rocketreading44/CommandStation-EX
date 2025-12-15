/*
 *  WebSocketInterface.cpp
 *  
 *  WebSocket server implementation for DCC-EX CommandStation
 *  Bridges browser WebSocket connections to the DCC-EX command parser
 */

#include "WebSocketInterface.h"

#if defined(ARDUINO_ARCH_ESP32)

#include "CommandDistributor.h"
#include "DCCEXParser.h"
#include "StringFormatter.h"
#include "DIAG.h"
#include "RingStream.h"

// Static member initialization
AsyncWebServer* WebSocketInterface::webServer = nullptr;
AsyncWebSocket* WebSocketInterface::ws = nullptr;
bool WebSocketInterface::enabled = false;
uint8_t WebSocketInterface::clientCount = 0;

// Response stream that captures output and sends to WebSocket client
class WebSocketPrint : public Print {
public:
    WebSocketPrint(AsyncWebSocketClient* c) : client(c), pos(0) {
        memset(buffer, 0, sizeof(buffer));
    }
    
    size_t write(uint8_t c) override {
        if (pos < sizeof(buffer) - 1) {
            buffer[pos++] = c;
            // Send on newline or buffer full
            if (c == '\n' || pos >= sizeof(buffer) - 2) {
                flush();
            }
        }
        return 1;
    }
    
    size_t write(const uint8_t* buf, size_t size) override {
        for (size_t i = 0; i < size; i++) {
            write(buf[i]);
        }
        return size;
    }
    
    void flush() {
        if (pos > 0 && client && client->status() == WS_CONNECTED) {
            buffer[pos] = '\0';
            client->text(buffer);
            pos = 0;
            memset(buffer, 0, sizeof(buffer));
        }
    }
    
private:
    AsyncWebSocketClient* client;
    char buffer[256];
    size_t pos;
};

void WebSocketInterface::setup() {
    // Initialize SPIFFS for serving web files
    if (!SPIFFS.begin(true)) {
        DIAG(F("WebSocket: SPIFFS mount failed"));
        return;
    }
    
    // Check if index.html exists
    if (!SPIFFS.exists("/index.html")) {
        DIAG(F("WebSocket: /index.html not found in SPIFFS"));
        DIAG(F("WebSocket: Upload data folder using ESP32 Sketch Data Upload"));
    }
    
    // Create web server and websocket
    webServer = new AsyncWebServer(WS_PORT);
    ws = new AsyncWebSocket("/ws");
    
    // WebSocket event handler
    ws->onEvent(onWebSocketEvent);
    webServer->addHandler(ws);
    
    // Serve static files from SPIFFS
    webServer->serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    
    // API endpoint to get server info
    webServer->on("/api/info", HTTP_GET, [](AsyncWebServerRequest* request) {
        String json = "{";
        json += "\"version\":\"DCC-EX WebSocket 1.0\",";
        json += "\"clients\":" + String(clientCount) + ",";
        json += "\"maxClients\":" + String(WS_MAX_CLIENTS);
        json += "}";
        request->send(200, "application/json", json);
    });
    
    // Handle 404
    webServer->onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not Found");
    });
    
    webServer->begin();
    enabled = true;
    
    DIAG(F("WebSocket: Server started on port %d"), WS_PORT);
    DIAG(F("WebSocket: Open http://%s/ in your browser"), WiFi.localIP().toString().c_str());
}

void WebSocketInterface::loop() {
    if (!enabled || !ws) return;
    
    // Clean up disconnected clients periodically
    static unsigned long lastCleanup = 0;
    if (millis() - lastCleanup > 1000) {
        ws->cleanupClients();
        lastCleanup = millis();
    }
}

void WebSocketInterface::broadcast(const char* message) {
    if (enabled && ws && clientCount > 0) {
        ws->textAll(message);
    }
}

bool WebSocketInterface::isEnabled() {
    return enabled;
}

void WebSocketInterface::onWebSocketEvent(AsyncWebSocket* server,
                                           AsyncWebSocketClient* client,
                                           AwsEventType type,
                                           void* arg,
                                           uint8_t* data,
                                           size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            if (clientCount >= WS_MAX_CLIENTS) {
                client->text("{\"error\":\"Max clients reached\"}");
                client->close();
                DIAG(F("WebSocket: Client rejected (max reached)"));
            } else {
                clientCount++;
                DIAG(F("WebSocket: Client #%u connected from %s"), 
                     client->id(), client->remoteIP().toString().c_str());
                // Send welcome message
                client->text("{\"connected\":true,\"clientId\":" + String(client->id()) + "}");
            }
            break;
            
        case WS_EVT_DISCONNECT:
            if (clientCount > 0) clientCount--;
            DIAG(F("WebSocket: Client #%u disconnected"), client->id());
            break;
            
        case WS_EVT_DATA:
            handleCommand(client, data, len);
            break;
            
        case WS_EVT_PONG:
            // Pong received
            break;
            
        case WS_EVT_ERROR:
            DIAG(F("WebSocket: Error on client #%u"), client->id());
            break;
    }
}

void WebSocketInterface::handleCommand(AsyncWebSocketClient* client, uint8_t* data, size_t len) {
    if (len == 0 || len > WS_COMMAND_BUFFER_SIZE) return;
    
    // Null-terminate the command
    char command[WS_COMMAND_BUFFER_SIZE + 1];
    memcpy(command, data, len);
    command[len] = '\0';
    
    // Trim whitespace
    char* cmd = command;
    while (*cmd == ' ' || *cmd == '\n' || *cmd == '\r') cmd++;
    size_t cmdLen = strlen(cmd);
    while (cmdLen > 0 && (cmd[cmdLen-1] == ' ' || cmd[cmdLen-1] == '\n' || cmd[cmdLen-1] == '\r')) {
        cmd[--cmdLen] = '\0';
    }
    
    if (cmdLen == 0) return;
    
    DIAG(F("WebSocket: CMD from #%u: %s"), client->id(), cmd);
    
    // Create a Print stream that sends responses back to this WebSocket client
    WebSocketPrint wsPrint(client);
    
    // Check if it looks like a DCC-EX command (starts with <)
    if (cmd[0] == '<') {
        // Parse as DCC-EX native command
        // The command includes the < > brackets
        DCCEXParser::parse(&wsPrint, (byte*)cmd, nullptr);
    } else {
        // Try parsing without brackets (user convenience)
        char bracketedCmd[WS_COMMAND_BUFFER_SIZE + 3];
        snprintf(bracketedCmd, sizeof(bracketedCmd), "<%s>", cmd);
        DCCEXParser::parse(&wsPrint, (byte*)bracketedCmd, nullptr);
    }
    
    // Flush any remaining output
    wsPrint.flush();
}

void WebSocketInterface::sendResponse(AsyncWebSocketClient* client, const char* response) {
    if (client && client->status() == WS_CONNECTED) {
        client->text(response);
    }
}

#endif // ARDUINO_ARCH_ESP32
