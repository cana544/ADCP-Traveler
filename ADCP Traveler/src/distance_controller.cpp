#include "distance_controller.h"

#include <cmath>
#include "config.h"

DistanceController::DistanceController(Encoder& encoder, MotorController& motor)
    : encoder_(encoder), motor_(motor), state_(State::IDLE), phaseStartUs_(0),
      lastUpdateUs_(0), settleStartUs_(0), requestedDistanceCm_(0),
      moveStartPositionCm_(0), targetPositionCm_(0), phaseDirection_(1),
      integralErrorM_(0), previousVoltage_(0) {}

void DistanceController::beginMove(float distanceCm, int direction) {
  if (isActive() || !std::isfinite(distanceCm) || distanceCm <= 0 ||
      (direction != 1 && direction != -1) ||
      !encoder_.isStopped(Config::Encoder::DISTANCE_STOP_TIMEOUT_US)) return;

  requestedDistanceCm_ = distanceCm;
  moveStartPositionCm_ = encoder_.positionCm();
  phaseDirection_ = direction;
  targetPositionCm_ = moveStartPositionCm_ + direction * distanceCm;
  phaseStartUs_ = lastUpdateUs_ = micros();
  settleStartUs_ = 0;
  integralErrorM_ = previousVoltage_ = 0;
  encoder_.setDistanceMode(true);
  encoder_.setDirection(direction);
  profile_.start(distanceCm, direction);
  motor_.setEnabled(true);
  state_ = State::MOVING;
}

void DistanceController::update() {
  if (isActive()) runControlPhase();
}

void DistanceController::runControlPhase() {
  const uint32_t now = micros();
  const float elapsed = (now - phaseStartUs_) * 1.0e-6f;
  if (elapsed > profile_.durationSeconds() + Config::Control::MOVE_TIMEOUT_MARGIN_S) {
    motor_.stop(); previousVoltage_ = 0;
    state_ = State::FAILED;
    encoder_.setDistanceMode(false);
    return;
  }
  const float dt = max(0.001f, min(0.2f, (now - lastUpdateUs_) * 1.0e-6f));
  lastUpdateUs_ = now;
  const MotionReference reference = profile_.sample(elapsed);
  const float position = encoder_.positionCm() - moveStartPositionCm_;
  const float velocity = encoder_.velocityCmS();
  const float positionError = reference.positionCm - position;

  if (state_ == State::MOVING && !reference.finished) {
    float command = reference.velocityCmS + Config::Control::POSITION_KP * positionError;
    command = phaseDirection_ * constrain(phaseDirection_ * command, 0.0f,
                                          Config::Motion::V_MAX_CM_S);
    const float error = (command - velocity) * 0.01f;
    const float candidateIntegral = constrain(integralErrorM_ + error * dt, -1.0f, 1.0f);
    const float friction = command == 0 ? 0 :
        phaseDirection_ * Config::Control::FRICTION_COMPENSATION_V;
    const float effort = Config::Control::FEEDFORWARD_GAIN_V_PER_M_S * command * 0.01f +
        friction + Config::Control::VELOCITY_KP * error +
        Config::Control::VELOCITY_KI * candidateIntegral;
    float voltage = phaseDirection_ * constrain(phaseDirection_ * effort, 0.0f,
                                                 Config::Control::SUPPLY_VOLTAGE);
    if (phaseDirection_ * effort >= 0 &&
        phaseDirection_ * effort <= Config::Control::SUPPLY_VOLTAGE)
      integralErrorM_ = candidateIntegral;
    voltage = constrain(voltage,
        previousVoltage_ - Config::Control::VOLTAGE_SLEW_V_S * dt,
        previousVoltage_ + Config::Control::VOLTAGE_SLEW_V_S * dt);
    previousVoltage_ = voltage;
    motor_.setVoltage(voltage);
  } else {
    if (state_ == State::MOVING) {
      state_ = State::SETTLING;
      settleStartUs_ = now;
    }
    motor_.stop(); previousVoltage_ = 0;
    // One continuous move: a missed endpoint is an error, never a restart.
    if (now - settleStartUs_ >= Config::Control::SETTLING_TIME_US && encoder_.isStopped()) {
      const float error = targetPositionCm_ - encoder_.positionCm();
      state_ = fabsf(error) <= Config::Control::POSITION_TOLERANCE_CM
                   ? State::COMPLETE : State::FAILED;
      encoder_.setDistanceMode(false);
    }
  }
}

void DistanceController::cancel() {
  stopAndDisable();
  state_ = State::CANCELLED;
  integralErrorM_ = previousVoltage_ = 0;
  encoder_.setDistanceMode(false);
}

void DistanceController::stopAndDisable() {
  motor_.stop();
  motor_.setEnabled(false);
}

bool DistanceController::isActive() const {
  return state_ == State::MOVING || state_ == State::SETTLING;
}

DistanceController::State DistanceController::state() const { return state_; }

const char* DistanceController::statusText() const {
  switch (state_) {
    case State::IDLE: return "IDLE";
    case State::MOVING:
    case State::SETTLING: return "MOVING";
    case State::COMPLETE: return "COMPLETE";
    case State::CANCELLED: return "STOPPED";
    case State::FAILED: return "ERROR";
  }
  return "IDLE";
}

float DistanceController::commandDistanceCm() const { return requestedDistanceCm_; }
float DistanceController::moveStartPositionCm() const { return moveStartPositionCm_; }
