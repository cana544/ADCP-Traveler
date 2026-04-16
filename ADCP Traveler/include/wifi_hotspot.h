#ifndef WIFI_HOTSPOT_H
#define WIFI_HOTSPOT_H

#include <Arduino.h>

#include "led_controller.h"

class WifiHotspot {
 public:
  WifiHotspot();

  void begin(uint8_t ledPin = 2);
  void update();
  bool isLedOn() const;

 private:
  uint8_t onboardLedPin_;
  LedController ledController_;

  void setLed(bool on);
  void setPwm(uint8_t value);
  void sendLedStateResponse();
  void sendWifiSignalResponse();
  bool serveFile(const char* path, const char* contentType);
  void handleRoot();
  void handleLedOn();
  void handleLedOff();
  void handleLedStatus();
  void handleLedPwm();
  void handleWifiSignal();
  void handleNotFound();
};

#endif
