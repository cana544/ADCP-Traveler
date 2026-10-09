#include "distance_controller.h"

#include <cmath>
#include "config.h"

DistanceController::DistanceController(Encoder& encoder, MotorController& motor)
    : encoder_(encoder), motor_(motor), state_(State::IDLE), phaseStartUs_(0),
      lastUpdateUs_(0), settleStartUs_(0), requestedDistanceCm_(0),
      moveStartPositionCm_(0), targetPositionCm_(0), phaseDirection_(1),
      integralErrorM_(0), previousVoltage_(0), profilePositionOffsetCm_(0),
      profileTimeOffsetS_(0) {}

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
  profilePositionOffsetCm_ = profileTimeOffsetS_ = 0;
  encoder_.setDistanceMode(true);
  encoder_.setDirection(direction);
  profile_.start(distanceCm, direction);
  motor_.stop();
  motor_.setEnabled(true);
  state_ = State::STARTING;
}

void DistanceController::update() {
  if (state_ == State::STARTING) runStartupPhase();
  else if (isActive()) runControlPhase();
}

void DistanceController::beginSettling(uint32_t now) {
  motor_.stop();
  previousVoltage_ = 0;
  state_ = State::SETTLING;
  settleStartUs_ = now;
}

void DistanceController::runStartupPhase() {
  const uint32_t now = micros();
  if (now - phaseStartUs_ >= Config::Control::STARTUP_TIMEOUT_US) {
    stopAndDisable();
    previousVoltage_ = integralErrorM_ = 0;
    state_ = State::FAILED;
    encoder_.setDistanceMode(false);
    return;
  }
  const float travelled = phaseDirection_ * (encoder_.positionCm() - moveStartPositionCm_);
  const float remaining = requestedDistanceCm_ - travelled;
  if (remaining <= 0) {
    beginSettling(now);
    return;
  }
  const float confirmationDistance = Config::Control::STARTUP_CONFIRM_PULSES *
      Config::Encoder::DISTANCE_PER_PULSE_CM;
  if (travelled >= confirmationDistance - 0.0001f && !encoder_.isStopped()) {
    const float measuredSpeed = max(0.0f, phaseDirection_ * encoder_.velocityCmS());
    const float brakingDistance = measuredSpeed * measuredSpeed /
        (2.0f * Config::Motion::A_MAX_CM_S2);
    if (remaining <= brakingDistance) {
      beginSettling(now);
      return;
    }
    // Enter the remaining-distance profile at the measured speed, rather than
    // restarting from rest or adding the startup travel to the endpoint.
    const float initialSpeed = min(measuredSpeed, Config::Motion::V_MAX_CM_S);
    const float initialDistance = initialSpeed * initialSpeed /
        (2.0f * Config::Motion::A_MAX_CM_S2);
    profile_.start(remaining + initialDistance, phaseDirection_);
    profileTimeOffsetS_ = initialSpeed / Config::Motion::A_MAX_CM_S2;
    profilePositionOffsetCm_ = encoder_.positionCm() - moveStartPositionCm_ -
        phaseDirection_ * initialDistance;
    phaseStartUs_ = lastUpdateUs_ = now;
    integralErrorM_ = 0;
    state_ = State::MOVING;
    return;
  }
  const float dt = max(0.0f, min(0.2f, (now - lastUpdateUs_) * 1.0e-6f));
  lastUpdateUs_ = now;
  const float maximum = min(Config::Control::STARTUP_MAX_VOLTAGE,
                            Config::Control::SUPPLY_VOLTAGE);
  previousVoltage_ = phaseDirection_ * min(maximum,
      fabsf(previousVoltage_) + Config::Control::STARTUP_RAMP_V_S * dt);
  motor_.setVoltage(previousVoltage_);
}

void DistanceController::runControlPhase() {
  const uint32_t now = micros();
  const float elapsed = (now - phaseStartUs_) * 1.0e-6f;
  const bool motionTimedOut = state_ == State::MOVING &&
      elapsed > profile_.durationSeconds() - profileTimeOffsetS_ +
                    Config::Control::MOVE_TIMEOUT_MARGIN_S;
  const bool settlingTimedOut = state_ == State::SETTLING &&
      now - settleStartUs_ > Config::Control::SETTLING_TIME_US +
          static_cast<uint32_t>(Config::Control::MOVE_TIMEOUT_MARGIN_S * 1000000.0f);
  if (motionTimedOut || settlingTimedOut) {
    motor_.stop(); previousVoltage_ = 0;
    state_ = State::FAILED;
    encoder_.setDistanceMode(false);
    return;
  }
  const float dt = max(0.001f, min(0.2f, (now - lastUpdateUs_) * 1.0e-6f));
  lastUpdateUs_ = now;
  MotionReference reference = profile_.sample(elapsed + profileTimeOffsetS_);
  reference.positionCm += profilePositionOffsetCm_;
  const float position = encoder_.positionCm() - moveStartPositionCm_;
  const float velocity = encoder_.velocityCmS();
  const float positionError = reference.positionCm - position;

  if (state_ == State::MOVING &&
      phaseDirection_ * (targetPositionCm_ - encoder_.positionCm()) <= 0) {
    beginSettling(now);
  }

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
      beginSettling(now);
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
  return state_ == State::STARTING || state_ == State::MOVING || state_ == State::SETTLING;
}

DistanceController::State DistanceController::state() const { return state_; }

const char* DistanceController::statusText() const {
  switch (state_) {
    case State::IDLE: return "IDLE";
    case State::STARTING:
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
