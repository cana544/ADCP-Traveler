#ifndef WIFI_HOTSPOT_H
#define WIFI_HOTSPOT_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "distance_controller.h"
#include "encoder.h"
#include "motor_controller.h"

class WifiHotspot {
 public:
  WifiHotspot();

  void begin(uint8_t rpwmPin = 25, uint8_t lpwmPin = 26, uint8_t renPin = 27,
             uint8_t lenPin = 14);
  void update();
  bool isMotorEnabled() const;
  void handleWebSocketEvent(AsyncWebSocketClient* client, AwsEventType type,
                            uint8_t* data, size_t len);

 private:
  MotorController motorController_;
  Encoder encoder_;
  DistanceController distanceController_;
  AsyncWebServer server_;
  AsyncWebSocket ws_;

  int pendingManualSpeed_;
  int manualDirection_;
  bool manualReversalPending_;
  uint32_t lastControlUs_;
  uint32_t lastStateBroadcastMs_;

  void setMotorEnabled(bool enabled);
  void setMotorSpeed(int speed);
  void applyManualSpeed(int speed);
  void updatePendingManualReversal();
  void cancelDistanceForManualControl();
  bool startDistanceMove(float distanceCm, int direction);
  void stopDistanceMove();
  bool zeroPosition();

  String makeStateJson() const;
  void sendMotorStateResponse(AsyncWebServerRequest* request);
  void sendDistanceStateResponse(AsyncWebServerRequest* request);
  void broadcastMotorState();
  void sendWifiSignalResponse(AsyncWebServerRequest* request);
  bool serveFile(AsyncWebServerRequest* request, const char* path,
                 const char* contentType);
  void handleRoot(AsyncWebServerRequest* request);
  void handleMotorOn(AsyncWebServerRequest* request);
  void handleMotorOff(AsyncWebServerRequest* request);
  void handleMotorStatus(AsyncWebServerRequest* request);
  void handleMotorSpeed(AsyncWebServerRequest* request);
  void handleDistanceStart(AsyncWebServerRequest* request);
  void handleDistanceStop(AsyncWebServerRequest* request);
  void handleDistanceZero(AsyncWebServerRequest* request);
  void handleDistanceStatus(AsyncWebServerRequest* request);
  void handleWifiSignal(AsyncWebServerRequest* request);
  void handleNotFound(AsyncWebServerRequest* request);
};

#endif
