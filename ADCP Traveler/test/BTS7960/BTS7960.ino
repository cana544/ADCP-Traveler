#include <Arduino.h>

// ESP32 pins for the BTS7960 driver.
// Picked to avoid common boot strapping pins.
constexpr uint8_t RPWM_PIN = 25;
constexpr uint8_t LPWM_PIN = 26;
constexpr uint8_t REN_PIN = 27;
constexpr uint8_t LEN_PIN = 14;

constexpr uint8_t RPWM_CHANNEL = 0;
constexpr uint8_t LPWM_CHANNEL = 1;
constexpr uint32_t PWM_FREQUENCY_HZ = 20000;
constexpr uint8_t PWM_RESOLUTION_BITS = 8;
constexpr uint8_t PWM_MAX_DUTY = 255;

constexpr uint8_t TARGET_MAX_PERCENT = 100;
constexpr uint8_t RAMP_STEP_PERCENT = 3;
constexpr unsigned long STEP_DELAY_MS = 120;
constexpr unsigned long HOLD_AT_MAX_MS = 1000;
constexpr unsigned long CYCLE_PAUSE_MS = 1500;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void attachPwmPin(uint8_t pin, uint8_t channel) {
  ledcAttachChannel(pin, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS, channel);
}

void writePwmDuty(uint8_t pin, uint8_t channel, int duty) {
  (void)channel;
  ledcWrite(pin, duty);
}
#else
void attachPwmPin(uint8_t pin, uint8_t channel) {
  ledcSetup(channel, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
  ledcAttachPin(pin, channel);
}

void writePwmDuty(uint8_t pin, uint8_t channel, int duty) {
  (void)pin;
  ledcWrite(channel, duty);
}
#endif

uint8_t percentToDuty(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }
  return static_cast<uint8_t>((static_cast<uint16_t>(PWM_MAX_DUTY) * percent) /
                              100);
}

void setMotorDuty(int duty) {
  duty = constrain(duty, -static_cast<int>(PWM_MAX_DUTY),
                   static_cast<int>(PWM_MAX_DUTY));

  if (duty > 0) {
    writePwmDuty(RPWM_PIN, RPWM_CHANNEL, duty);
    writePwmDuty(LPWM_PIN, LPWM_CHANNEL, 0);
  } else if (duty < 0) {
    writePwmDuty(RPWM_PIN, RPWM_CHANNEL, 0);
    writePwmDuty(LPWM_PIN, LPWM_CHANNEL, -duty);
  } else {
    writePwmDuty(RPWM_PIN, RPWM_CHANNEL, 0);
    writePwmDuty(LPWM_PIN, LPWM_CHANNEL, 0);
  }
}

void stopMotor() { setMotorDuty(0); }

void rampDirection(bool clockwise) {
  Serial.println(clockwise ? "CW ramp up" : "CCW ramp up");

  for (uint8_t percent = 0; percent <= TARGET_MAX_PERCENT;
       percent += RAMP_STEP_PERCENT) {
    int duty = percentToDuty(percent);
    setMotorDuty(clockwise ? duty : -duty);

    Serial.print(clockwise ? "CW duty: " : "CCW duty: ");
    Serial.print(percent);
    Serial.println("%");

    delay(STEP_DELAY_MS);
  }

  delay(HOLD_AT_MAX_MS);

  Serial.println(clockwise ? "CW ramp down" : "CCW ramp down");

  for (int percent = TARGET_MAX_PERCENT; percent >= 0;
       percent -= RAMP_STEP_PERCENT) {
    int duty = percentToDuty(static_cast<uint8_t>(percent));
    setMotorDuty(clockwise ? duty : -duty);
    delay(STEP_DELAY_MS);
  }

  stopMotor();
  delay(CYCLE_PAUSE_MS);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(REN_PIN, OUTPUT);
  pinMode(LEN_PIN, OUTPUT);
  digitalWrite(REN_PIN, HIGH);
  digitalWrite(LEN_PIN, HIGH);

  attachPwmPin(RPWM_PIN, RPWM_CHANNEL);
  attachPwmPin(LPWM_PIN, LPWM_CHANNEL);

  stopMotor();

  Serial.println("BTS7960 motor test ready.");
  Serial.println(
      "Sequence: 0 -> 100% CW, back to 0, then 0 -> 100% CCW, back to 0.");
}

void loop() {
  rampDirection(true);
  rampDirection(false);
}