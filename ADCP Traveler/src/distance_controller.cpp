#include "distance_controller.h"
#include <cmath>
#include "config.h"

DistanceController::DistanceController(Encoder& encoder, MotorController& motor)
    : encoder_(encoder), motor_(motor) {}

float DistanceController::remainingCm() const {
  return direction_ * (targetPositionCm_ - encoder_.positionCm());
}

void DistanceController::beginMove(float distanceCm, int direction) {
  if (isActive() || !std::isfinite(distanceCm) || distanceCm <= 0 || distanceCm > 100000.0f ||
      (direction != 1 && direction != -1) ||
      !encoder_.isStopped(Config::Encoder::DISTANCE_STOP_TIMEOUT_US)) return;
  direction_ = direction; requestedDistanceCm_ = distanceCm;
  moveStartPositionCm_ = encoder_.positionCm();
  targetPositionCm_ = moveStartPositionCm_ + direction * distanceCm;
  startUs_ = phaseUs_ = lastUpdateUs_ = progressUs_ = micros();
  const float seconds = min(Config::Control::TOTAL_TIMEOUT_MAX_S,
      Config::Control::TOTAL_TIMEOUT_BASE_S + distanceCm / 2.0f);
  deadlineUs_ = static_cast<uint32_t>(seconds * 1.0e6f);
  ++moveId_; failure_ = ""; recoveries_ = 0; stableUs_ = 0;
  transitionReason_ = "GO"; brakingLimited_ = false;
  commandCmS_ = requestedV_ = appliedV_ = slewV_ = integralV_ = candidateStartV_ = 0;
  learningEligible_ = startCandidatePending_ = false;
  activeLoadV_ = loadAdaptationEnabled_ ? learnedLoadV_[slot()] : 0;
  encoder_.setDistanceMode(true); encoder_.setDirection(direction);
  phasePulses_ = lastPulses_ = encoder_.pulseCount();
  motor_.setLoggingEnabled(false);
  motor_.stop(); motor_.setEnabled(true); state_ = State::STARTING;
}

float DistanceController::feedforward(float speed) const {
  return speed > 0 ? Config::Control::FRICTION_COMPENSATION_V +
      Config::Control::FEEDFORWARD_GAIN_V_PER_M_S * speed * .01f : 0;
}

void DistanceController::apply(float effort, float dt, float slew) {
  requestedV_ = effort;
  const float limited = constrain(effort, 0.0f, Config::Control::SUPPLY_VOLTAGE);
  slewV_ = constrain(limited, max(0.0f, slewV_ - slew * dt), slewV_ + slew * dt);
  motor_.setVoltage(direction_ * slewV_);
  appliedV_ = fabsf(motor_.currentSpeed()) * Config::Control::SUPPLY_VOLTAGE / 255;
}

void DistanceController::finish(State state, const char* reason) {
  motor_.stop(); appliedV_ = slewV_ = requestedV_ = commandCmS_ = integralV_ = 0;
  state_ = state; failure_ = reason;
  transitionReason_ = *reason ? reason : state == State::COMPLETE ? "SETTLED" : "STOP";
  learningEligible_ = startCandidatePending_ = false;
  encoder_.setDistanceMode(false);
  if (state == State::CANCELLED) motor_.setEnabled(false);
  motor_.setLoggingEnabled(true);
}

void DistanceController::settle(uint32_t now) {
  motor_.stop(); appliedV_ = slewV_ = requestedV_ = commandCmS_ = 0;
  state_ = State::SETTLING; phaseUs_ = now; learningEligible_ = false; stableUs_ = 0;
  transitionReason_ = "TARGET_BAND";
}

void DistanceController::recover(uint32_t now) {
  if (recoveries_ >= Config::Control::MAX_RECOVERIES) { finish(State::FAILED, "NO_PROGRESS"); return; }
  ++recoveries_; settle(now); state_ = State::RECOVERING;
  transitionReason_ = "NO_PROGRESS";
  phasePulses_ = encoder_.pulseCount(); integralV_ = 0; startCandidatePending_ = false;
}

void DistanceController::handoff(uint32_t now, float speed) {
  commandCmS_ = min(speed, Config::Motion::V_MAX_CM_S);
  const float p = Config::Control::VELOCITY_KP * (commandCmS_ - speed) * .01f;
  integralV_ = constrain(appliedV_ - feedforward(commandCmS_) - activeLoadV_ - p,
      -Config::Control::INTEGRAL_LIMIT_V, Config::Control::INTEGRAL_LIMIT_V);
  transitionReason_ = state_ == State::STARTING ? "START_PROGRESS" : "RECOVERY_PROGRESS";
  state_ = remainingCm() <= Config::Control::APPROACH_DISTANCE_CM ? State::APPROACHING : State::MOVING;
  phaseUs_ = progressUs_ = now; stableUs_ = 0;
}

void DistanceController::update() {
  if (!isActive()) return;
  const uint32_t now = micros(), gap = now - lastUpdateUs_;
  lastUpdateUs_ = now;
  if (!motor_.isEnabled()) { finish(State::CANCELLED, "DISABLED"); return; }
  const float remaining = remainingCm();
  if (!std::isfinite(remaining)) { finish(State::FAILED, "INVALID_FEEDBACK"); return; }
  if (remaining < -Config::Control::POSITION_TOLERANCE_CM - Config::Control::POSITION_EPSILON_CM) {
    finish(State::FAILED, "OVERRUN"); return;
  }
  if (now - startUs_ >= deadlineUs_) { finish(State::FAILED, "MOVE_TIMEOUT"); return; }
  if (state_ == State::STARTING && now - phaseUs_ >= Config::Control::STARTUP_TIMEOUT_US) {
    finish(State::FAILED, "START_TIMEOUT"); return;
  }
  if (gap > Config::Control::MAX_CONTROL_GAP_US && state_ != State::SETTLING) {
    finish(State::FAILED, "CONTROL_GAP"); return;
  }
  const float dt = min(.2f, gap * 1.0e-6f);
  const int32_t pulses = encoder_.pulseCount();
  if (pulses != lastPulses_) { lastPulses_ = pulses; progressUs_ = now; }
  const float speed = max(0.0f, direction_ * encoder_.velocityCmS());
  learningEligible_ = false;
  if (remaining <= Config::Control::POSITION_TOLERANCE_CM + Config::Control::POSITION_EPSILON_CM &&
      state_ != State::SETTLING) settle(now);
  if (state_ == State::SETTLING) {
    if (now - phaseUs_ >= Config::Control::SETTLING_TIME_US && encoder_.isStopped()) {
      if (fabsf(remaining) <= Config::Control::POSITION_TOLERANCE_CM + Config::Control::POSITION_EPSILON_CM)
        finish(State::COMPLETE);
      else recover(now);
    }
    return;
  }
  // Brake toward the far accepted edge, retaining margin and reaction allowance.
  // The absolute target and symmetric completion checks are unchanged.
  const float room = max(0.0f, remaining + Config::Control::POSITION_TOLERANCE_CM -
      Config::Control::STOP_MARGIN_CM - speed * Config::Control::REACTION_TIME_S);
  const float ceiling = sqrtf(2 * Config::Control::BRAKING_CM_S2 * room);
  brakingLimited_ = speed > ceiling;
  // Effort ramps also need stopping protection before their normal PI handoff.
  if ((state_ == State::STARTING || state_ == State::RECOVERING) && speed > ceiling) {
    finish(State::FAILED, "BRAKING_LIMIT"); return;
  }
  if (state_ == State::STARTING) {
    if (direction_ * (pulses - phasePulses_) >= Config::Control::STARTUP_CONFIRM_PULSES && speed > 0) {
      candidateStartV_ = appliedV_; startCandidatePending_ = true; handoff(now, speed); return;
    }
    const float learned = startAdaptationEnabled_ ? learnedStartV_[slot()] : 0;
    const float ramp = appliedV_ < learned * .6f ? 1.5f : Config::Control::STARTUP_RAMP_V_S;
    apply(min(Config::Control::STARTUP_MAX_VOLTAGE, slewV_ + ramp * dt), dt, ramp);
    return;
  }
  if (state_ == State::RECOVERING) {
    const uint32_t elapsed = now - phaseUs_;
    if (elapsed >= Config::Control::RECOVERY_PAUSE_US + Config::Control::RECOVERY_TIMEOUT_US) {
      recover(now); return;
    }
    if (elapsed < Config::Control::RECOVERY_PAUSE_US) return;
    if (pulses != phasePulses_) { handoff(now, min(speed, Config::Control::APPROACH_SPEED_CM_S)); return; }
    apply(min(Config::Control::RECOVERY_MAX_VOLTAGE, slewV_ + Config::Control::RECOVERY_RAMP_V_S * dt),
          dt, Config::Control::RECOVERY_RAMP_V_S);
    return;
  }
  if (now - progressUs_ >= Config::Control::NO_PROGRESS_US) { recover(now); return; }
  if (remaining <= Config::Control::APPROACH_DISTANCE_CM && state_ != State::APPROACHING) {
    state_ = State::APPROACHING; transitionReason_ = "APPROACH_DISTANCE";
  }
  const float desired = min(ceiling, state_ == State::APPROACHING ? Config::Control::APPROACH_SPEED_CM_S
                                                              : Config::Motion::V_MAX_CM_S);
  const float acceleration = desired < commandCmS_ ? Config::Control::BRAKING_CM_S2 : Config::Motion::A_MAX_CM_S2;
  commandCmS_ += constrain(desired - commandCmS_, -acceleration * dt, acceleration * dt);
  // Infeasible measured braking envelope: stopping takes precedence over smoothness.
  if (room == 0) commandCmS_ = 0;
  const float error = (commandCmS_ - speed) * .01f;
  const float p = Config::Control::VELOCITY_KP * error;
  const float effort = feedforward(commandCmS_) + activeLoadV_ + p + integralV_;
  apply(commandCmS_ > 0 ? effort : 0, dt, Config::Control::VOLTAGE_SLEW_V_S);
  const bool realizable = fabsf(requestedV_ - appliedV_) < .08f &&
      requestedV_ > 0 && requestedV_ < Config::Control::SUPPLY_VOLTAGE;
  if (realizable || error * (requestedV_ - appliedV_) < 0)
    integralV_ = constrain(integralV_ + Config::Control::VELOCITY_KI * error * dt,
        -Config::Control::INTEGRAL_LIMIT_V, Config::Control::INTEGRAL_LIMIT_V);
  const bool stable = state_ == State::MOVING && commandCmS_ > 5 &&
      fabsf(desired - commandCmS_) < .2f && fabsf(error) < .1f && speed > 5 && realizable &&
      now - progressUs_ < 500000UL && !learningInhibited_ &&
      (startAdaptationEnabled_ || loadAdaptationEnabled_);
  if (!stable) stableUs_ = 0;
  else {
    if (stableUs_ == 0) stableUs_ = now;
    learningEligible_ = now - stableUs_ >= Config::Control::STABLE_LEARN_US;
  }
  if (learningEligible_) {
    if (startCandidatePending_ && startAdaptationEnabled_) {
      float& learned = learnedStartV_[slot()];
      learned = constrain(learned + Config::Control::START_LEARN_ALPHA * (candidateStartV_ - learned),
                          0.0f, Config::Control::START_LEARN_LIMIT_V);
      startCandidatePending_ = false;
    }
    if (loadAdaptationEnabled_) {
      const float transfer = constrain(integralV_, -Config::Control::LOAD_LEARN_V_S * dt,
                                                   Config::Control::LOAD_LEARN_V_S * dt);
      const float bias = constrain(activeLoadV_ + transfer, -Config::Control::LOAD_BIAS_LIMIT_V,
                                                           Config::Control::LOAD_BIAS_LIMIT_V);
      integralV_ -= bias - activeLoadV_; activeLoadV_ = bias; learnedLoadV_[slot()] = bias;
    }
  }
}

void DistanceController::cancel() { finish(State::CANCELLED); }
bool DistanceController::isActive() const {
  return state_ == State::STARTING || state_ == State::MOVING || state_ == State::APPROACHING ||
         state_ == State::SETTLING || state_ == State::RECOVERING;
}
const char* DistanceController::statusText() const {
  if (isActive()) return "MOVING";
  if (state_ == State::COMPLETE) return "COMPLETE";
  if (state_ == State::CANCELLED) return "STOPPED";
  if (state_ == State::FAILED) return "ERROR";
  return "IDLE";
}
const char* DistanceController::phaseText() const {
  switch (state_) {
    case State::STARTING: return "STARTING";
    case State::MOVING: return "TRAVELLING";
    case State::APPROACHING: return "APPROACHING";
    case State::SETTLING: return "SETTLING";
    case State::RECOVERING: return "RECOVERING";
    default: return statusText();
  }
}
void DistanceController::setAdaptationEnabled(bool enabled) { setAdaptationEnabled(enabled, enabled); }
void DistanceController::setAdaptationEnabled(bool startEnabled, bool loadEnabled) {
  if (isActive()) return;
  startAdaptationEnabled_ = startEnabled; loadAdaptationEnabled_ = loadEnabled;
}
bool DistanceController::resetLearning() {
  if (isActive()) return false;
  for (int i = 0; i < 2; ++i) learnedLoadV_[i] = learnedStartV_[i] = 0;
  activeLoadV_ = 0; return true;
}
float DistanceController::learnedStartVoltage(int direction) const { return learnedStartV_[direction > 0 ? 1 : 0]; }
float DistanceController::learnedLoadVoltage(int direction) const { return learnedLoadV_[direction > 0 ? 1 : 0]; }
DistanceController::Telemetry DistanceController::telemetry() const {
  return {moveId_, micros() - startUs_, deadlineUs_, micros() - progressUs_, targetPositionCm_, remainingCm(),
      requestedSpeedCmS(), direction_ * requestedV_, appliedVoltage(), integralV_, activeLoadV_,
      learnedStartV_[slot()], recoveries_, startAdaptationEnabled_ || loadAdaptationEnabled_, learningEligible_,
      startAdaptationEnabled_, loadAdaptationEnabled_};
}
