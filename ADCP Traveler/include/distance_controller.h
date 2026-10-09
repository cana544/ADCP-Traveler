#ifndef DISTANCE_CONTROLLER_H
#define DISTANCE_CONTROLLER_H

#include <Arduino.h>

#include "encoder.h"
#include "motion_profile.h"
#include "motor_controller.h"

class DistanceController {
 public:
  enum class State {
    IDLE,
    MOVING,
    SETTLING,
    COMPLETE,
    CANCELLED,
    FAILED
  };

  DistanceController(Encoder& encoder, MotorController& motor);

  void beginMove(float distanceCm, int direction);
  void update();
  void cancel();

  bool isActive() const;
  State state() const;
  const char* statusText() const;
  float commandDistanceCm() const;
  float moveStartPositionCm() const;

 private:
  Encoder& encoder_;
  MotorController& motor_;
  MotionProfile profile_;

  State state_;
  uint32_t phaseStartUs_;
  uint32_t lastUpdateUs_;
  uint32_t settleStartUs_;
  float requestedDistanceCm_;
  float moveStartPositionCm_;
  float targetPositionCm_;
  int phaseDirection_;
  float integralErrorM_;
  float previousVoltage_;

  void runControlPhase();
  void stopAndDisable();
};

#endif
