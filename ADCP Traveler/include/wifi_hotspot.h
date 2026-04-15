#ifndef WIFI_HOTSPOT_H
#define WIFI_HOTSPOT_H

#include <Arduino.h>

class WifiHotspot {
 public:
  WifiHotspot();

  void begin(uint8_t ledPin = 2);
  void update();
  bool isLedOn() const;

 private:
  uint8_t onboardLedPin_;
  bool ledOn_;

  void setLed(bool on);
  void sendLedStateResponse();
  void handleRoot();
  void handleLedOn();
  void handleLedOff();
  void handleLedStatus();
  void handleNotFound();
};

#endif
