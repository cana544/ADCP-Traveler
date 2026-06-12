#include <Arduino.h>

#include "config.h"
#include "wifi_hotspot.h"

WifiHotspot wifiHotspot;

void setup() {
  Serial.begin(115200);
  delay(1000);

  wifiHotspot.begin(Config::Pins::MOTOR_RPWM, Config::Pins::MOTOR_LPWM,
                    Config::Pins::MOTOR_REN, Config::Pins::MOTOR_LEN);
}

void loop() { wifiHotspot.update(); }
