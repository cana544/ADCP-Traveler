#include <Arduino.h>

#if __has_include(<esp_arduino_version.h>)
#include <esp_arduino_version.h>
#endif

// BTS7960 motor driver pins (ESP32-friendly defaults).
constexpr uint8_t RPWM_PIN = 25;
constexpr uint8_t LPWM_PIN = 26;
constexpr uint8_t REN_PIN = 27;
constexpr uint8_t LEN_PIN = 14;

// Single-channel encoder input.
constexpr uint8_t ENCODER_PIN = 21;

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
constexpr unsigned long REPORT_INTERVAL_MS = 10UL;
constexpr float PULSES_PER_REV = 50.0f;

#define ISR_ATTR IRAM_ATTR

void attachPwmPin(uint8_t pin, uint8_t channel) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)channel;
  ledcAttach(pin, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
#else
  ledcSetup(channel, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS);
  ledcAttachPin(pin, channel);
#endif
}

void writePwmDuty(uint8_t pin, uint8_t channel, int duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)channel;
  ledcWrite(pin, duty);
#else
  (void)pin;
  ledcWrite(channel, duty);
#endif
}

volatile bool commandedCW = true;
volatile uint32_t totalPulses = 0;
volatile long signedPulseCount = 0;
volatile uint32_t lastPulseUs = 0;
volatile uint32_t latestPeriodUs = 0;

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
    commandedCW = true;
    writePwmDuty(RPWM_PIN, RPWM_CHANNEL, duty);
    writePwmDuty(LPWM_PIN, LPWM_CHANNEL, 0);
  } else if (duty < 0) {
    commandedCW = false;
    writePwmDuty(RPWM_PIN, RPWM_CHANNEL, 0);
    writePwmDuty(LPWM_PIN, LPWM_CHANNEL, -duty);
  } else {
    writePwmDuty(RPWM_PIN, RPWM_CHANNEL, 0);
    writePwmDuty(LPWM_PIN, LPWM_CHANNEL, 0);
  }
}

void stopMotor() { setMotorDuty(0); }

void ISR_ATTR countPulse() {
  uint32_t nowUs = micros();

  if (lastPulseUs == 0) {
    lastPulseUs = nowUs;
    totalPulses++;
    signedPulseCount += commandedCW ? 1 : -1;
    return;
  }

  uint32_t periodUs = nowUs - lastPulseUs;
  latestPeriodUs = periodUs;
  lastPulseUs = nowUs;
  totalPulses++;
  signedPulseCount += commandedCW ? 1 : -1;
}

void reportEncoderCsv() {
  static uint32_t lastReportMs = 0;

  uint32_t nowMs = millis();
  if (nowMs - lastReportMs < REPORT_INTERVAL_MS) {
    return;
  }
  lastReportMs = nowMs;

  uint32_t pulsesTotal;
  long pulsesSigned;
  uint32_t periodUs;
  bool cw;

  noInterrupts();
  pulsesTotal = totalPulses;
  pulsesSigned = signedPulseCount;
  periodUs = latestPeriodUs;
  cw = commandedCW;
  interrupts();

  float rpm = 0.0f;
  if (pulsesTotal > 1 && periodUs > 0) {
    rpm = 60000000.0f / (PULSES_PER_REV * periodUs);
    if (!cw) {
      rpm = -rpm;
    }
  }

  float angleDeg = 0.0f;
  if (PULSES_PER_REV > 0.0f) {
    angleDeg = (static_cast<float>(pulsesSigned) / PULSES_PER_REV) * 360.0f;
  }

  Serial.print(nowMs);
  Serial.print(",");
  Serial.print(rpm, 2);
  Serial.print(",");
  Serial.print(angleDeg, 2);
  Serial.print(",");
  Serial.print(pulsesSigned);
  Serial.print(",");
  Serial.println(cw ? "CW" : "CCW");
}

void delayWithReporting(unsigned long durationMs) {
  unsigned long startMs = millis();
  while (millis() - startMs < durationMs) {
    reportEncoderCsv();
    delay(1);
  }
}

void rampDirection(bool clockwise) {
  for (uint8_t percent = 0; percent <= TARGET_MAX_PERCENT;
       percent += RAMP_STEP_PERCENT) {
    int duty = percentToDuty(percent);
    setMotorDuty(clockwise ? duty : -duty);
    delayWithReporting(STEP_DELAY_MS);
  }

  delayWithReporting(HOLD_AT_MAX_MS);

  for (int percent = TARGET_MAX_PERCENT; percent >= 0;
       percent -= RAMP_STEP_PERCENT) {
    int duty = percentToDuty(static_cast<uint8_t>(percent));
    setMotorDuty(clockwise ? duty : -duty);
    delayWithReporting(STEP_DELAY_MS);
  }

  stopMotor();
  delayWithReporting(CYCLE_PAUSE_MS);
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

  pinMode(ENCODER_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN), countPulse, RISING);

  stopMotor();

  Serial.println("time_ms,rpm,angle_deg,pulse_count,rotation_direction");
}

void loop() {
  rampDirection(true);
  rampDirection(false);
}
