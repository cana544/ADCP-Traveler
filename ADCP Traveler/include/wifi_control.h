#ifndef WIFI_CONTROL_H
#define WIFI_CONTROL_H

#include <Arduino.h>

namespace WifiControl {
extern const char *const AP_SSID;
extern const char *const AP_PASSWORD;

void begin(uint8_t ledPin = 2);
void update();
bool isLedOn();
}

#endif
