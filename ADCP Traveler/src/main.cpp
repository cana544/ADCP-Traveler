#include <Arduino.h>

#include "wifi_control.h"

namespace {
constexpr uint8_t ONBOARD_LED_PIN = 2;
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1000);

  WifiControl::begin(ONBOARD_LED_PIN);
}

void loop() {
  WifiControl::update();
}
