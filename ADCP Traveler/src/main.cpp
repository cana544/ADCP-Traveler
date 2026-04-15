#include <Arduino.h>

#include "config.h"
#include "wifi_hotspot.h"

WifiHotspot wifiHotspot;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(Config::Pins::ONBOARD_LED, OUTPUT);
  wifiHotspot.begin(Config::Pins::ONBOARD_LED);
}

void loop() { wifiHotspot.update(); }
