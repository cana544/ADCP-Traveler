#include "distance_controller.h"

#include <Arduino.h>

#include "config.h"

DistanceController::DistanceController(Encoder& encoder, MotorController& motor)
    : encoder_(encoder),
      motor_(motor),
      state_(State::IDLE),
      phaseStartUs_(0),
      requestedDistanceCm_(0.0f),
      moveStartPositionCm_(0.0f),
      targetPositionCm_(0.0f),
      phaseStartPositionCm_(0.0f),
      phaseDirection_(1),
      integralErrorM_(0.0f),
      previousVelocityErrorM_S_(0.0f),
      completeConfirmCount_(0) {}

void DistanceController::beginMove(float distanceCm, int direction) {
  if (distanceCm <= 0.0f) {
    return;
  }

  requestedDistanceCm_ = distanceCm;
  moveStartPositionCm_ = encoder_.positionCm();
  targetPositionCm_ =
      moveStartPositionCm_ + (direction >= 0 ? distanceCm : -distanceCm);
  completeConfirmCount_ = 0;
  integralErrorM_ = 0.0f;
  previousVelocityErrorM_S_ = 0.0f;

  motor_.setEnabled(true);
  startPhase(distanceCm, direction >= 0 ? 1 : -1);
}

void DistanceController::startPhase(float distanceCm, int direction) {
  phaseDirection_ = direction >= 0 ? 1 : -1;
  phaseStartPositionCm_ = encoder_.positionCm();
  phaseStartUs_ = micros();
  integralErrorM_ = 0.0f;
  previousVelocityErrorM_S_ = 0.0f;
  completeConfirmCount_ = 0;

  encoder_.setDirection(phaseDirection_);
  profile_.start(distanceCm, phaseDirection_);
  state_ = State::MOVING;
}

void DistanceController::update() {
  if (state_ == State::MOVING || state_ == State::SETTLING) {
    runControlPhase();
    return;
  }

  if (state_ == State::REVERSAL_WAIT) {
    motor_.stop();
    if (encoder_.isStopped()) {
      startCorrectionIfNeeded();
    }
  }
}

void DistanceController::runControlPhase() {
  const float elapsedSeconds =
      static_cast<float>(micros() - phaseStartUs_) * 1.0e-6f;
  const MotionReference reference = profile_.sample(elapsedSeconds);

  const float measuredPositionCm = encoder_.positionCm();
  const float measuredVelocityCmS = encoder_.velocityCmS();
  const float desiredPositionCm = phaseStartPositionCm_ + reference.positionCm;
  const float positionErrorCm = desiredPositionCm - measuredPositionCm;

  const float velocityCorrectionCmS =
      Config::Control::POSITION_KP * positionErrorCm;
  float velocityCommandCmS = reference.velocityCmS + velocityCorrectionCmS;

  if (phaseDirection_ > 0) {
    velocityCommandCmS = constrain(velocityCommandCmS, 0.0f,
                                   Config::Motion::V_MAX_CM_S);
  } else {
    velocityCommandCmS = constrain(velocityCommandCmS,
                                   -Config::Motion::V_MAX_CM_S, 0.0f);
  }

  const float velocityErrorM_S =
      (velocityCommandCmS - measuredVelocityCmS) / 100.0f;
  const float dt = static_cast<float>(Config::Control::CONTROL_PERIOD_US) * 1.0e-6f;

  integralErrorM_ += velocityErrorM_S * dt;
  const float derivativeErrorM_S2 =
      (velocityErrorM_S - previousVelocityErrorM_S_) / dt;
  previousVelocityErrorM_S_ = velocityErrorM_S;

  const float feedbackVoltage =
      Config::Control::VELOCITY_KP * velocityErrorM_S +
      Config::Control::VELOCITY_KI * integralErrorM_ +
      Config::Control::VELOCITY_KD * derivativeErrorM_S2;

  const float feedforwardVoltage =
      Config::Control::FEEDFORWARD_GAIN_V_PER_M_S *
      (reference.velocityCmS / 100.0f);

  float effortVoltage = constrain(
      feedbackVoltage + feedforwardVoltage,
      -Config::Control::SUPPLY_VOLTAGE,
      Config::Control::SUPPLY_VOLTAGE);

  if ((phaseDirection_ > 0 && effortVoltage < 0.0f) ||
      (phaseDirection_ < 0 && effortVoltage > 0.0f)) {
    effortVoltage = 0.0f;
  }

  motor_.setVoltage(effortVoltage);

  if (!reference.finished) {
    state_ = State::MOVING;
    return;
  }

  state_ = State::SETTLING;
  const float targetErrorCm = targetPositionCm_ - measuredPositionCm;

  if (fabsf(targetErrorCm) <= Config::Control::POSITION_TOLERANCE_CM &&
      fabsf(measuredVelocityCmS) <= Config::Control::VELOCITY_TOLERANCE_CM_S &&
      encoder_.isStopped()) {
    completeConfirmCount_++;
    if (completeConfirmCount_ >= Config::Control::COMPLETE_CONFIRM_CYCLES) {
      motor_.stop();
      state_ = State::COMPLETE;
    }
    return;
  }

  completeConfirmCount_ = 0;

  const bool correctionNeedsReverse =
      (phaseDirection_ > 0 && targetErrorCm < -Config::Control::POSITION_TOLERANCE_CM) ||
      (phaseDirection_ < 0 && targetErrorCm > Config::Control::POSITION_TOLERANCE_CM);

  if (correctionNeedsReverse) {
    motor_.stop();
    state_ = State::REVERSAL_WAIT;
  }
}

void DistanceController::startCorrectionIfNeeded() {
  const float errorCm = targetPositionCm_ - encoder_.positionCm();

  if (fabsf(errorCm) <= Config::Control::POSITION_TOLERANCE_CM) {
    state_ = State::COMPLETE;
    return;
  }

  startPhase(fabsf(errorCm), errorCm >= 0.0f ? 1 : -1);
}

void DistanceController::cancel() {
  stopAndDisable();
  state_ = State::CANCELLED;
  completeConfirmCount_ = 0;
}

void DistanceController::stopAndDisable() {
  motor_.stop();
  motor_.setEnabled(false);
}

bool DistanceController::isActive() const {
  return state_ == State::MOVING || state_ == State::SETTLING ||
         state_ == State::REVERSAL_WAIT;
}

DistanceController::State DistanceController::state() const { return state_; }

const char* DistanceController::statusText() const {
  switch (state_) {
    case State::IDLE:
      return "IDLE";
    case State::MOVING:
      return "MOVING";
    case State::SETTLING:
      return "MOVING";
    case State::REVERSAL_WAIT:
      return "MOVING";
    case State::COMPLETE:
      return "COMPLETE";
    case State::CANCELLED:
      return "STOPPED";
  }
  return "IDLE";
}

float DistanceController::commandDistanceCm() const {
  return requestedDistanceCm_;
}

float DistanceController::moveStartPositionCm() const {
  return moveStartPositionCm_;
}
