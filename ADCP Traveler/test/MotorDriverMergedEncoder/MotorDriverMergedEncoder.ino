#include <Arduino.h>

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
constexpr unsigned long REPORT_INTERVAL_MS = 40;
constexpr float PULSES_PER_REV = 50.0f;
constexpr uint32_t MIN_PULSE_PERIOD_US = 2000UL;
constexpr uint32_t STOP_TIMEOUT_US = 1000000UL;
constexpr float RPM_FILTER_ALPHA = 0.60f;

#if defined(ESP32)
#define ISR_ATTR IRAM_ATTR
#else
#define ISR_ATTR
#endif

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

volatile bool commandedCW = true;
volatile long signedPulseCount = 0;
volatile uint32_t lastPulseUs = 0;
volatile uint32_t latestPeriodUs = 0;

static uint32_t lastReportMs = 0;
static float filteredRPM = 0.0f;

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
    signedPulseCount += commandedCW ? 1 : -1;
    return;
  }

  uint32_t periodUs = nowUs - lastPulseUs;

  if (periodUs >= MIN_PULSE_PERIOD_US) {
    latestPeriodUs = periodUs;
    lastPulseUs = nowUs;
    signedPulseCount += commandedCW ? 1 : -1;
  }
}

void reportEncoderCsv() {
  uint32_t nowMs = millis();

  if (lastReportMs == 0) {
    lastReportMs = nowMs;
    return;
  }

  if (nowMs - lastReportMs < REPORT_INTERVAL_MS) {
    return;
  }

  long pulsesSigned;
  uint32_t periodUs;
  uint32_t pulseTimeUs;
  bool cw;

  noInterrupts();
  pulsesSigned = signedPulseCount;
  periodUs = latestPeriodUs;
  pulseTimeUs = lastPulseUs;
  cw = commandedCW;
  interrupts();

  uint32_t timeSincePulseUs =
      (pulseTimeUs == 0) ? UINT32_MAX : (micros() - pulseTimeUs);

  if (pulseTimeUs == 0 || timeSincePulseUs > STOP_TIMEOUT_US) {
    filteredRPM = 0.0f;
  } else if (periodUs > 0) {
    // Use the larger of the last period and current time-since-pulse so
    // reported RPM decays in real-time during ramp-down, instead of staying
    // stale.
    uint32_t effectivePeriodUs = periodUs;
    if (timeSincePulseUs > effectivePeriodUs) {
      effectivePeriodUs = timeSincePulseUs;
    }

    float measuredRPM = 60000000.0f / (PULSES_PER_REV * effectivePeriodUs);
    if (!cw) {
      measuredRPM = -measuredRPM;
    }

    if (filteredRPM == 0.0f) {
      filteredRPM = measuredRPM;
    } else {
      filteredRPM = RPM_FILTER_ALPHA * measuredRPM +
                    (1.0f - RPM_FILTER_ALPHA) * filteredRPM;
    }
  }

  float angleDeg = 0.0f;
  if (PULSES_PER_REV > 0.0f) {
    angleDeg = (static_cast<float>(pulsesSigned) / PULSES_PER_REV) * 360.0f;
  }

  Serial.print(nowMs);
  Serial.print(',');
  Serial.print(filteredRPM, 2);
  Serial.print(',');
  Serial.print(angleDeg, 2);
  Serial.print(',');
  Serial.print(pulsesSigned);
  Serial.print(',');
  Serial.println(cw ? "CW" : "CCW");

  lastReportMs = nowMs;
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
