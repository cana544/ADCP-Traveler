#ifndef WIFI_HOTSPOT_H
#define WIFI_HOTSPOT_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "led_controller.h"

class WifiHotspot {
 public:
  WifiHotspot();

  void begin(uint8_t ledPin = 2);
  void update();
  bool isLedOn() const;
  void handleWebSocketEvent(AsyncWebSocketClient* client, AwsEventType type,
                            uint8_t* data, size_t len);

 private:
  uint8_t onboardLedPin_;
  LedController ledController_;
  AsyncWebServer server_;
  AsyncWebSocket ws_;

  void setLed(bool on);
  void setPwm(uint8_t value);
  void sendLedStateResponse(AsyncWebServerRequest* request);
  void sendWifiSignalResponse(AsyncWebServerRequest* request);
  bool serveFile(AsyncWebServerRequest* request, const char* path,
                 const char* contentType);
  void handleRoot(AsyncWebServerRequest* request);
  void handleLedOn(AsyncWebServerRequest* request);
  void handleLedOff(AsyncWebServerRequest* request);
  void handleLedStatus(AsyncWebServerRequest* request);
  void handleLedPwm(AsyncWebServerRequest* request);
  void handleWifiSignal(AsyncWebServerRequest* request);
  void handleNotFound(AsyncWebServerRequest* request);
};

#endif
