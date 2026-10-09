#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

namespace Config {
namespace Wifi {
constexpr char AP_SSID[] = "Gauge Glide Traveller";
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
// Validated distance-mode estimator; manual-mode timing stays unchanged.
constexpr uint32_t DISTANCE_STOP_TIMEOUT_US = 500000UL;
constexpr uint32_t DISTANCE_MIN_PULSE_INTERVAL_US = 15000UL;
constexpr uint8_t DISTANCE_FILTER_SAMPLES = 3;
}  // namespace Encoder

namespace Motion {
constexpr float V_MAX_CM_S = 20.0f;
constexpr float A_MAX_CM_S2 = 10.0f;
}  // namespace Motion

namespace Control {
constexpr float POSITION_KP = 3.0f;
constexpr float VELOCITY_KP = 13.0f;
constexpr float VELOCITY_KI = 2.0f;
constexpr float VELOCITY_KD = 0.0f;
constexpr float SUPPLY_VOLTAGE = 12.0f;
constexpr float FEEDFORWARD_GAIN_V_PER_M_S = 23.0f;
constexpr float FRICTION_COMPENSATION_V = 1.8f;
constexpr float VOLTAGE_SLEW_V_S = 25.0f;
constexpr float STARTUP_RAMP_V_S = 1.0f;
constexpr float STARTUP_MAX_VOLTAGE = 8.0f;
constexpr uint32_t STARTUP_TIMEOUT_US = 10000000UL;
constexpr uint8_t STARTUP_CONFIRM_PULSES = 2;
constexpr float MOVE_TIMEOUT_MARGIN_S = 5.0f;
constexpr uint32_t SETTLING_TIME_US = 1000000UL;
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
