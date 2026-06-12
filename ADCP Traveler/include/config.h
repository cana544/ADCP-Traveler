#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

namespace Config {
namespace Wifi {
constexpr char AP_SSID[] = "ESP32-Motor-Control";
constexpr char AP_PASSWORD[] = "password";
}  // namespace Wifi

namespace Pins {
constexpr uint8_t MOTOR_RPWM = 25;
constexpr uint8_t MOTOR_LPWM = 26;
constexpr uint8_t MOTOR_REN = 27;
constexpr uint8_t MOTOR_LEN = 14;
}

namespace Files {
constexpr char ACCEL_FULL_CALIBRATION_PATH[] = "/accel_full_calibration.json";
constexpr char ACCEL_CALIBRATION_PATH[] = "/accel_calibration.json";
}  // namespace Files
}  // namespace Config

#endif
