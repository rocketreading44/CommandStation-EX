/*
 *  WebSocketInterface.h
 *  
 *  WebSocket server for DCC-EX CommandStation
 *  Allows browser-based throttles to connect directly to the command station
 *  
 *  Add to CommandStation-EX by:
 *  1. Copy WebSocketInterface.h and WebSocketInterface.cpp to your CommandStation-EX folder
 *  2. Add #include "WebSocketInterface.h" to CommandStation-EX.ino
 *  3. Call WebSocketInterface::setup() in setup() after WiFi is initialized
 *  4. Call WebSocketInterface::loop() in the main loop()
 *  5. Upload the data/ folder contents to SPIFFS using ESP32 Sketch Data Upload
 *  
 *  Requires: ESPAsyncWebServer library
 *            AsyncTCP library
 *            
 *  Add to platformio.ini:
 *    lib_deps = 
 *      ESP32Async/ESPAsyncWebServer
 *      ESP32Async/AsyncTCP
 */

#ifndef WebSocketInterface_h
#define WebSocketInterface_h

#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32)

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

// Configuration
#define WS_PORT 80                    // HTTP/WebSocket port
#define WS_MAX_CLIENTS 5              // Maximum concurrent WebSocket clients
#define WS_COMMAND_BUFFER_SIZE 128    // Max command length

class WebSocketInterface {
public:
    static void setup();
    static void loop();
    static void broadcast(const char* message);
    static bool isEnabled();
    
private:
    static void onWebSocketEvent(AsyncWebSocket* server, 
                                  AsyncWebSocketClient* client,
                                  AwsEventType type, 
                                  void* arg, 
                                  uint8_t* data, 
                                  size_t len);
    static void handleCommand(AsyncWebSocketClient* client, uint8_t* data, size_t len);
    static void sendResponse(AsyncWebSocketClient* client, const char* response);
    
    static AsyncWebServer* webServer;
    static AsyncWebSocket* ws;
    static bool enabled;
    static uint8_t clientCount;
};

#else
// Stub for non-ESP32 platforms
class WebSocketInterface {
public:
    static void setup() {}
    static void loop() {}
    static void broadcast(const char* message) { (void)message; }
    static bool isEnabled() { return false; }
};
#endif

#endif
