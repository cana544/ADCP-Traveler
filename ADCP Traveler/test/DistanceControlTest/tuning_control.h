#pragma once
#include <stdio.h>
#include <string.h>
#include <cmath>
using std::isfinite;

struct TuningSettings {
  float vmax = 20.0f;
  float amax = 10.0f;
  float positionKp = 3.0f;
  float velocityKp = 13.0f;
  float velocityKi = 2.0f;
  float feedforward = 23.0f;
  float friction = 1.8f;
};
TuningSettings tuningSettings;

class MotionProfile {
 public:
  void start(float distanceCm, int direction) {
    distance_ = fabsf(distanceCm);
    direction_ = direction >= 0 ? 1 : -1;
    acceleration_ = tuningSettings.amax;
    peak_ = min(tuningSettings.vmax, sqrtf(distance_ * acceleration_));
    accelTime_ = peak_ / acceleration_;
    cruiseTime_ = peak_ > 0.0f ? max(0.0f, distance_ / peak_ - accelTime_) : 0.0f;
    duration_ = 2.0f * accelTime_ + cruiseTime_;
  }
  MotionReference sample(float seconds) const {
    const float t = max(0.0f, seconds);
    float x = 0.0f, v = 0.0f;
    if (t >= duration_) return {direction_ * distance_, 0.0f, true};
    if (t < accelTime_) {
      v = acceleration_ * t;
      x = 0.5f * acceleration_ * t * t;
    } else if (t < accelTime_ + cruiseTime_) {
      v = peak_;
      x = 0.5f * peak_ * accelTime_ + peak_ * (t - accelTime_);
    } else {
      const float remaining = duration_ - t;
      v = acceleration_ * remaining;
      x = distance_ - 0.5f * acceleration_ * remaining * remaining;
    }
    return {direction_ * x, direction_ * v, false};
  }
  float duration() const { return duration_; }
  const char* type() const { return cruiseTime_ > 0.0001f ? "TRAPEZOIDAL" : "TRIANGULAR"; }
 private:
  float distance_ = 0, acceleration_ = 10, peak_ = 0;
  float accelTime_ = 0, cruiseTime_ = 0, duration_ = 0;
  int direction_ = 1;
};

class DistanceController {
 public:
  enum class State { IDLE, MOVING, SETTLING, COMPLETE, FAILED };
  DistanceController(Encoder& encoder, MotorController& motor)
      : encoder_(encoder), motor_(motor) {}
  bool beginMove(float distanceCm, int direction) {
    if (isActive() || !isfinite(distanceCm) || distanceCm < 10 || distanceCm > 500 ||
        (direction != 1 && direction != -1) || !encoder_.isStopped()) return false;
    distance_ = distanceCm;
    direction_ = direction;
    startPosition_ = encoder_.positionCm();
    startUs_ = lastUpdateUs_ = lastHeartbeatUs_ = micros();
    settleUs_ = 0;
    integral_ = previousVoltage_ = 0;
    terminalSent_ = false;
    profile_.start(distanceCm, direction);
    encoder_.setDirection(direction);
    state_ = State::MOVING;
    motor_.setEnabled(true);
    Serial.print("DC_EVENT,START,"); Serial.print(distanceCm, 3);
    Serial.print(','); Serial.print(direction); Serial.print(',');
    Serial.print(profile_.type()); Serial.print(','); Serial.println(profile_.duration(), 4);
    Serial.println("time_s,distance_cm,velocity_cm_s,effort_v,pwm,ref_velocity_cm_s,velocity_cmd_cm_s,ref_position_cm,position_error_cm,state,pulse_age_us,pulse_period_us,pulses");
    return true;
  }
  void heartbeat() { lastHeartbeatUs_ = micros(); }
  void stop(const char* reason = "STOPPED") {
    motor_.stop();
    previousVoltage_ = 0;
    if (isActive()) { state_ = State::FAILED; terminal(reason); }
  }
  bool isActive() const { return state_ == State::MOVING || state_ == State::SETTLING; }
  State state() const { return state_; }
  const char* stateText() const {
    switch (state_) {
      case State::IDLE: return "IDLE";
      case State::MOVING: return "MOVING";
      case State::SETTLING: return "SETTLING";
      case State::COMPLETE: return "COMPLETE";
      default: return "FAILED";
    }
  }
  void update() {
    const uint32_t now = micros();
    if (!isActive()) {
      // Continue measuring for a full second after the terminal event.
      if (terminalSent_ && now - terminalUs_ <= 1200000UL) {
        const float elapsed = (now - startUs_) * 1.0e-6f;
        const auto ref = profile_.sample(elapsed);
        const float position = encoder_.positionCm() - startPosition_;
        logSample(elapsed, position, encoder_.velocityCmS(), ref,
                  ref.positionCm - position, 0, 0);
      }
      return;
    }
    if (now - lastHeartbeatUs_ > 2000000UL) { stop("HOST_TIMEOUT"); return; }
    const float elapsed = (now - startUs_) * 1.0e-6f;
    if (elapsed > profile_.duration() + 5.0f) { stop("MOVE_TIMEOUT"); return; }
    const float dt = max(0.001f, min(0.2f, (now - lastUpdateUs_) * 1.0e-6f));
    lastUpdateUs_ = now;
    const MotionReference reference = profile_.sample(elapsed);
    const float position = encoder_.positionCm() - startPosition_;
    const float velocity = encoder_.velocityCmS();
    const float positionError = reference.positionCm - position;
    float command = 0, voltage = 0;
    if (state_ == State::MOVING && !reference.finished) {
      command = reference.velocityCmS + tuningSettings.positionKp * positionError;
      // No commanded reversal during a trial: overshoot is a failed trial,
      // not a second corrective move with ambiguous single-channel direction.
      command = direction_ * constrain(direction_ * command, 0.0f, tuningSettings.vmax);
      const float error = (command - velocity) * 0.01f;
      const float candidateIntegral = constrain(integral_ + error * dt, -1.0f, 1.0f);
      const float friction = command == 0 ? 0 : direction_ * tuningSettings.friction;
      const float unconstrained = tuningSettings.feedforward * command * 0.01f + friction +
          tuningSettings.velocityKp * error + tuningSettings.velocityKi * candidateIntegral;
      voltage = direction_ * constrain(direction_ * unconstrained, 0.0f, 12.0f);
      if (direction_ * unconstrained >= 0 && direction_ * unconstrained <= 12) integral_ = candidateIntegral;
      // Slew rate is expressed in V/s, not per-loop increments.
      voltage = constrain(voltage, previousVoltage_ - 25.0f * dt, previousVoltage_ + 25.0f * dt);
      previousVoltage_ = voltage;
      motor_.setVoltage(voltage);
    } else {
      if (state_ == State::MOVING) { state_ = State::SETTLING; settleUs_ = now; }
      motor_.stop(); previousVoltage_ = 0;
      // End at the planned stop; never creep, hunt, or restart to obtain a
      // deceptively small final error. Failed tracking must be retuned.
      if (now - settleUs_ >= 1000000UL && encoder_.isStopped()) {
        const float error = direction_ * distance_ - position;
        state_ = fabsf(error) <= Config::EncoderConfig::DISTANCE_PER_PULSE_CM
                     ? State::COMPLETE : State::FAILED;
      }
    }
    logSample(elapsed, position, velocity, reference, positionError, command, voltage);
    if (!isActive()) terminal(state_ == State::COMPLETE ? "COMPLETE" : "ENDPOINT_ERROR");
  }
 private:
  Encoder& encoder_;
  MotorController& motor_;
  MotionProfile profile_;
  State state_ = State::IDLE;
  uint32_t startUs_ = 0, lastUpdateUs_ = 0, lastHeartbeatUs_ = 0, settleUs_ = 0;
  float startPosition_ = 0, distance_ = 0, integral_ = 0, previousVoltage_ = 0;
  int direction_ = 1;
  bool terminalSent_ = false;
  uint32_t terminalUs_ = 0;
  void terminal(const char* reason) {
    if (terminalSent_) return;
    terminalSent_ = true;
    terminalUs_ = micros();
    Serial.print("DC_EVENT,END,"); Serial.print(reason); Serial.print(',');
    Serial.println(direction_ * distance_ - (encoder_.positionCm() - startPosition_), 4);
  }
  void logSample(float elapsed, float position, float velocity,
                 const MotionReference& ref, float error, float command, float voltage) {
    uint32_t pulseUs, period; int32_t pulses;
    noInterrupts(); pulseUs = encoder_.lastPulseUs_; period = encoder_.latestPeriodUs_;
    pulses = encoder_.signedPulses_; interrupts();
    Serial.print(elapsed, 4); Serial.print(','); Serial.print(position, 4);
    Serial.print(','); Serial.print(velocity, 4); Serial.print(','); Serial.print(voltage, 4);
    Serial.print(','); Serial.print(motor_.currentSpeed()); Serial.print(','); Serial.print(ref.velocityCmS, 4);
    Serial.print(','); Serial.print(command, 4); Serial.print(','); Serial.print(ref.positionCm, 4);
    Serial.print(','); Serial.print(error, 4); Serial.print(','); Serial.print(stateText());
    Serial.print(','); Serial.print(pulseUs ? micros() - pulseUs : 0);
    Serial.print(','); Serial.print(period); Serial.print(','); Serial.println(pulses);
  }
};

MotorController motorController;
Encoder encoder;
DistanceController distanceController(encoder, motorController);
uint32_t lastControlUs = 0;
char serialLine[180];
size_t serialLength = 0;
bool serialOverflow = false;

bool handleCommand(const char* line) {
  if (strcmp(line, "PING") == 0) { distanceController.heartbeat(); return true; }
  if (strcmp(line, "STOP") == 0) { distanceController.stop(); Serial.println("DC_ACK,STOP"); return true; }
  if (strcmp(line, "STATUS") == 0) {
    Serial.print("DC_STATUS,"); Serial.print(distanceController.stateText());
    Serial.print(','); Serial.println(motorController.currentSpeed()); return true;
  }
  if (strcmp(line, "HELLO") == 0) { Serial.println("DC_EVENT,READY,V2"); return true; }
  float distance; int direction; char extra;
  if (sscanf(line, "MOVE %f %d %c", &distance, &direction, &extra) == 2) {
    if (distanceController.beginMove(distance, direction)) return true;
    Serial.println("DC_ERROR,MOVE_REJECTED"); return false;
  }
  TuningSettings candidate;
  if (sscanf(line, "CONFIG %f %f %f %f %f %f %f %c", &candidate.vmax, &candidate.amax,
      &candidate.positionKp, &candidate.velocityKp, &candidate.velocityKi,
      &candidate.feedforward, &candidate.friction, &extra) == 7) {
    if (distanceController.isActive() || !isfinite(candidate.vmax) || !isfinite(candidate.amax) ||
        !isfinite(candidate.positionKp) || !isfinite(candidate.velocityKp) || !isfinite(candidate.velocityKi) ||
        !isfinite(candidate.feedforward) || !isfinite(candidate.friction) ||
        candidate.vmax < 5 || candidate.vmax > 30 || candidate.amax < 2 || candidate.amax > 15 ||
        candidate.positionKp < 0 || candidate.positionKp > 20 || candidate.velocityKp < 0 || candidate.velocityKp > 100 ||
        candidate.velocityKi < 0 || candidate.velocityKi > 100 || candidate.feedforward < 0 || candidate.feedforward > 60 ||
        candidate.friction < 0 || candidate.friction > 4) {
      Serial.println("DC_ERROR,CONFIG_REJECTED"); return false;
    }
    tuningSettings = candidate; Serial.println("DC_ACK,CONFIG"); return true;
  }
  Serial.println("DC_ERROR,INVALID_COMMAND"); return false;
}

void setup() {
  Serial.begin(115200);
  motorController.begin(Config::Pins::MOTOR_RPWM, Config::Pins::MOTOR_LPWM,
                        Config::Pins::MOTOR_REN, Config::Pins::MOTOR_LEN);
  motorController.stop();
  encoder.begin(Config::Pins::ENCODER);
  encoder.zero();
  lastControlUs = micros();
  Serial.println("DC_EVENT,READY,V2");
}

void loop() {
  while (Serial.available()) {
    const char ch = Serial.read();
    if (ch == '\n') {
      serialLine[serialLength] = '\0';
      if (!serialOverflow) handleCommand(serialLine);
      else Serial.println("DC_ERROR,LINE_TOO_LONG");
      serialLength = 0; serialOverflow = false;
    } else if (ch != '\r') {
      if (serialLength < sizeof(serialLine) - 1) serialLine[serialLength++] = ch;
      else serialOverflow = true;
    }
  }
  const uint32_t now = micros();
  if (now - lastControlUs >= Config::Control::CONTROL_PERIOD_US) {
    lastControlUs = now;
    encoder.update(); distanceController.update();
  }
}
