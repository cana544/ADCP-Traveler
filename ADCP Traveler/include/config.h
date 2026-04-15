#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

namespace Config {
namespace Wifi {
constexpr char AP_SSID[] = "ESP32-LED-Control";
constexpr char AP_PASSWORD[] = "password";
}

namespace Pins {
constexpr uint8_t ONBOARD_LED = 2;
}
}  // namespace Config

#endif
