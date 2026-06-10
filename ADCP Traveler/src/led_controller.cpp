#include "led_controller.h"

LedController::LedController(uint8_t pwmChannel, uint32_t frequency,
                             uint8_t resolutionBits)
    : ledPin_(2),
      pwmChannel_(pwmChannel),
      frequency_(frequency),
      resolutionBits_(resolutionBits),
      ledOn_(false),
      currentPwm_(0) {}

void LedController::begin(uint8_t ledPin) {
  ledPin_ = ledPin;
  pinMode(ledPin_, OUTPUT);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttachChannel(ledPin_, frequency_, resolutionBits_, pwmChannel_);
#else
  ledcSetup(pwmChannel_, frequency_, resolutionBits_);
  ledcAttachPin(ledPin_, pwmChannel_);
#endif

  setLed(false);
}

void LedController::writePwm(uint8_t value) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(ledPin_, value);
#else
  ledcWrite(pwmChannel_, value);
#endif
}

void LedController::setLed(bool on) {
  ledOn_ = on;

  if (on && currentPwm_ == 0) {
    currentPwm_ = DEFAULT_ON_PWM;
  }

  if (on) {
    writePwm(currentPwm_);
  } else {
    writePwm(0);
  }

  logState("setLed");
}

void LedController::setPwm(uint8_t value) {
  currentPwm_ = value;

  ledOn_ = value > 0;
  writePwm(value);

  logState("setPwm");
}

bool LedController::isOn() const { return ledOn_; }

uint8_t LedController::currentPwm() const { return currentPwm_; }

void LedController::logState(const char* source) const {
  const int brightnessPercent =
      (static_cast<int>(currentPwm_) * 100 + 127) / 255;
  Serial.printf("[LED] %-9s | State: %-3s | Brightness: %3d%% | PWM: %3u\n",
                source, ledOn_ ? "ON" : "OFF", brightnessPercent, currentPwm_);
}
