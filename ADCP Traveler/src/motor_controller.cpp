#include "motor_controller.h"

MotorController::MotorController(uint8_t rpwmChannel, uint8_t lpwmChannel,
                                 uint32_t frequency, uint8_t resolutionBits)
    : rpwmPin_(25),
      lpwmPin_(26),
      renPin_(27),
      lenPin_(14),
      rpwmChannel_(rpwmChannel),
      lpwmChannel_(lpwmChannel),
      frequency_(frequency),
      resolutionBits_(resolutionBits),
      enabled_(false),
      currentSpeed_(0) {}

void MotorController::begin(uint8_t rpwmPin, uint8_t lpwmPin, uint8_t renPin,
                            uint8_t lenPin) {
  rpwmPin_ = rpwmPin;
  lpwmPin_ = lpwmPin;
  renPin_ = renPin;
  lenPin_ = lenPin;

  pinMode(renPin_, OUTPUT);
  pinMode(lenPin_, OUTPUT);

  attachPwmPin(rpwmPin_, rpwmChannel_);
  attachPwmPin(lpwmPin_, lpwmChannel_);

  stop();
  setEnabled(true);
}

void MotorController::attachPwmPin(uint8_t pin, uint8_t channel) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttachChannel(pin, frequency_, resolutionBits_, channel);
#else
  ledcSetup(channel, frequency_, resolutionBits_);
  ledcAttachPin(pin, channel);
#endif
}

void MotorController::writePwmDuty(uint8_t pin, uint8_t channel, int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  (void)channel;
  ledcWrite(pin, duty);
#else
  (void)pin;
  ledcWrite(channel, duty);
#endif
}

void MotorController::setEnabled(bool enabled) {
  enabled_ = enabled;
  digitalWrite(renPin_, enabled_ ? HIGH : LOW);
  digitalWrite(lenPin_, enabled_ ? HIGH : LOW);

  if (!enabled_) {
    stop();
  } else {
    applySpeed();
  }

  logState(enabled_ ? "enable" : "disable");
}

void MotorController::setSpeed(int speed) {
  currentSpeed_ = constrain(speed, -MAX_DUTY, MAX_DUTY);

  if (!enabled_) {
    setEnabled(true);
  } else {
    applySpeed();
    logState("setSpeed");
  }
}

void MotorController::stop() {
  currentSpeed_ = 0;
  applySpeed();
  logState("stop");
}

bool MotorController::isEnabled() const { return enabled_; }

int MotorController::currentSpeed() const { return currentSpeed_; }

void MotorController::applySpeed() {
  if (!enabled_) {
    writePwmDuty(rpwmPin_, rpwmChannel_, 0);
    writePwmDuty(lpwmPin_, lpwmChannel_, 0);
    return;
  }

  if (currentSpeed_ > 0) {
    writePwmDuty(rpwmPin_, rpwmChannel_, currentSpeed_);
    writePwmDuty(lpwmPin_, lpwmChannel_, 0);
  } else if (currentSpeed_ < 0) {
    writePwmDuty(rpwmPin_, rpwmChannel_, 0);
    writePwmDuty(lpwmPin_, lpwmChannel_, -currentSpeed_);
  } else {
    writePwmDuty(rpwmPin_, rpwmChannel_, 0);
    writePwmDuty(lpwmPin_, lpwmChannel_, 0);
  }
}

void MotorController::logState(const char* source) const {
  const char* direction = "STOP";
  if (currentSpeed_ > 0) {
    direction = "CW";
  } else if (currentSpeed_ < 0) {
    direction = "CCW";
  }

  const int speedPercent = (abs(currentSpeed_) * 100 + 127) / 255;
  Serial.printf("[MOTOR] %-9s | Enabled: %-3s | Direction: %-4s | Speed: "
                "%3d%% | PWM: %+4d\n",
                source, enabled_ ? "YES" : "NO", direction, speedPercent,
                currentSpeed_);
}
