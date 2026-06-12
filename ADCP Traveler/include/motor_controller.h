#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

#include <Arduino.h>

class MotorController {
 public:
  MotorController(uint8_t rpwmChannel = 0, uint8_t lpwmChannel = 1,
                  uint32_t frequency = 20000, uint8_t resolutionBits = 8);

  void begin(uint8_t rpwmPin, uint8_t lpwmPin, uint8_t renPin, uint8_t lenPin);
  void setEnabled(bool enabled);
  void setSpeed(int speed);
  void stop();
  bool isEnabled() const;
  int currentSpeed() const;

 private:
  static constexpr int MAX_DUTY = 255;

  uint8_t rpwmPin_;
  uint8_t lpwmPin_;
  uint8_t renPin_;
  uint8_t lenPin_;
  uint8_t rpwmChannel_;
  uint8_t lpwmChannel_;
  uint32_t frequency_;
  uint8_t resolutionBits_;
  bool enabled_;
  int currentSpeed_;

  void attachPwmPin(uint8_t pin, uint8_t channel);
  void writePwmDuty(uint8_t pin, uint8_t channel, int duty);
  void applySpeed();
  void logState(const char* source) const;
};

#endif
