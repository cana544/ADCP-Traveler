#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Arduino.h>

class LedController {
 public:
  explicit LedController(uint8_t pwmChannel = 0, uint32_t frequency = 5000,
                         uint8_t resolutionBits = 8);

  void begin(uint8_t ledPin);
  void setLed(bool on);
  void setPwm(uint8_t value);
  bool isOn() const;
  uint8_t currentPwm() const;

 private:
  uint8_t ledPin_;
  uint8_t pwmChannel_;
  uint32_t frequency_;
  uint8_t resolutionBits_;
  bool ledOn_;
  uint8_t currentPwm_;

  void logState(const char* source) const;
};

#endif