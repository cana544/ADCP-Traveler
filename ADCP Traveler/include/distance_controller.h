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
    REVERSAL_WAIT,
    COMPLETE,
    CANCELLED
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
  float requestedDistanceCm_;
  float moveStartPositionCm_;
  float targetPositionCm_;
  float phaseStartPositionCm_;
  int phaseDirection_;
  float integralErrorM_;
  float previousVelocityErrorM_S_;
  uint8_t completeConfirmCount_;

  void startPhase(float distanceCm, int direction);
  void runControlPhase();
  void startCorrectionIfNeeded();
  void stopAndDisable();
};

#endif
