#include <Arduino.h>

#include "config.h"
#include "wifi_hotspot.h"

WifiHotspot wifiHotspot;

void runLedBootTest(uint8_t ledPin) {
  Serial.printf("Testing LED pin GPIO%u...\n", ledPin);
  pinMode(ledPin, OUTPUT);

  for (uint8_t i = 0; i < 3; ++i) {
    digitalWrite(ledPin, HIGH);
    delay(200);
    digitalWrite(ledPin, LOW);
    delay(200);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  runLedBootTest(Config::Pins::ONBOARD_LED);
  wifiHotspot.begin(Config::Pins::ONBOARD_LED);
}

void loop() { wifiHotspot.update(); }
