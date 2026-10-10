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
constexpr uint8_t BATTERY_ADC = 34;
}  // namespace Pins

namespace Battery {
constexpr float R1_OHMS = 47000.0f;
constexpr float R2_OHMS = 10000.0f;
constexpr float ADC_CORRECTION = 1.0f;
constexpr uint32_t SAMPLE_PERIOD_MS = 1000;
constexpr uint32_t SAMPLE_SPACING_MS = 2;
constexpr uint8_t SAMPLE_COUNT = 16;
constexpr float FILTER_ALPHA = 0.1f;  // About ten seconds of smoothing.
// Reject disconnected/grounded inputs, saturation, and implausible 3S voltages.
constexpr float MIN_VALID_VOLTAGE = 6.0f;
constexpr float MAX_VALID_VOLTAGE = 13.2f;
constexpr uint32_t MAX_ADC_MV = 3000;
struct ChargePoint { float voltage; float percent; };
// Ascending voltage; initial estimates pending physical discharge testing.
constexpr ChargePoint CHARGE_TABLE[] = {
    {10.50f, 0}, {11.10f, 10}, {11.40f, 20}, {11.70f, 40},
    {12.00f, 60}, {12.30f, 80}, {12.60f, 100}};
}  // namespace Battery

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

namespace Section {
constexpr uint32_t SCAN_RAMP_DURATION_US = 4000000UL;
}  // namespace Section

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
