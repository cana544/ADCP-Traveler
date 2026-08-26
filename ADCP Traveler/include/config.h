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
constexpr uint8_t ENCODER = 21;
}  // namespace Pins

namespace Encoder {
constexpr float SLOTS_PER_REV = 50.0f;
constexpr float WHEEL_RADIUS_CM = 5.0f;
constexpr float DISTANCE_PER_PULSE_CM =
    (2.0f * PI * WHEEL_RADIUS_CM) / SLOTS_PER_REV;
constexpr uint32_t STOP_TIMEOUT_US = 250000UL;
constexpr uint8_t VELOCITY_FILTER_SAMPLES = 4;
}  // namespace Encoder

namespace Motion {
constexpr float V_MAX_CM_S = 50.0f;
constexpr float A_MAX_CM_S2 = 25.0f;
}  // namespace Motion

namespace Control {
constexpr float POSITION_KP = 1.0f;
constexpr float VELOCITY_KP = 10.0f;
constexpr float VELOCITY_KI = 0.0f;
constexpr float VELOCITY_KD = 0.0f;
constexpr float SUPPLY_VOLTAGE = 12.0f;
constexpr float FEEDFORWARD_GAIN_V_PER_M_S = 26.9625f;
constexpr float POSITION_TOLERANCE_CM = Encoder::DISTANCE_PER_PULSE_CM;
constexpr float VELOCITY_TOLERANCE_CM_S = 1.0f;
constexpr uint8_t COMPLETE_CONFIRM_CYCLES = 5;
constexpr uint32_t CONTROL_PERIOD_US = 50000UL;
}  // namespace Control

namespace Files {
constexpr char ACCEL_FULL_CALIBRATION_PATH[] = "/accel_full_calibration.json";
constexpr char ACCEL_CALIBRATION_PATH[] = "/accel_calibration.json";
}  // namespace Files
}  // namespace Config

#endif
