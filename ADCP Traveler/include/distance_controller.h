#ifndef DISTANCE_CONTROLLER_H
#define DISTANCE_CONTROLLER_H
#include <Arduino.h>
#include "encoder.h"
#include "motor_controller.h"
class DistanceController {
 public:
  enum class State { IDLE, STARTING, MOVING, APPROACHING, SETTLING, RECOVERING, COMPLETE, CANCELLED, FAILED };
  struct Telemetry {
    uint32_t moveId, elapsedUs, deadlineUs, progressAgeUs;
    float targetCm, remainingCm, commandCmS, requestedV, appliedV, integralV, loadV, startV;
    uint8_t recoveries;
    bool adaptationEnabled, learningEligible, startAdaptationEnabled, loadAdaptationEnabled;
  };
  DistanceController(Encoder& encoder, MotorController& motor);
  void beginMove(float distanceCm, int direction);
  void update();
  void cancel();
  bool isActive() const;
  State state() const { return state_; }
  const char* statusText() const;
  const char* phaseText() const;
  const char* failureText() const { return failure_; }
  const char* transitionReason() const { return transitionReason_; }
  bool brakingLimited() const { return brakingLimited_; }
  float commandDistanceCm() const { return requestedDistanceCm_; }
  float moveStartPositionCm() const { return moveStartPositionCm_; }
  float targetPositionCm() const { return targetPositionCm_; }
  float remainingCm() const;
  float requestedSpeedCmS() const { return direction_ * commandCmS_; }
  float appliedVoltage() const { return direction_ * appliedV_; }
  Telemetry telemetry() const;
  void setAdaptationEnabled(bool enabled);
  void setAdaptationEnabled(bool startEnabled, bool loadEnabled);
  bool startAdaptationEnabled() const { return startAdaptationEnabled_; }
  bool loadAdaptationEnabled() const { return loadAdaptationEnabled_; }
  bool resetLearning();
  void setLearningInhibited(bool inhibited) { learningInhibited_ = inhibited; }
  float learnedStartVoltage(int direction) const;
  float learnedLoadVoltage(int direction) const;
 private:
  Encoder& encoder_;
  MotorController& motor_;
  State state_ = State::IDLE;
  const char* failure_ = "";
  const char* transitionReason_ = "";
  bool brakingLimited_ = false;
  uint32_t moveId_ = 0, startUs_ = 0, phaseUs_ = 0, lastUpdateUs_ = 0, progressUs_ = 0, stableUs_ = 0, deadlineUs_ = 0;
  int32_t lastPulses_ = 0, phasePulses_ = 0;
  int direction_ = 1;
  float requestedDistanceCm_ = 0, moveStartPositionCm_ = 0, targetPositionCm_ = 0,
        commandCmS_ = 0, requestedV_ = 0, appliedV_ = 0, slewV_ = 0, integralV_ = 0,
        candidateStartV_ = 0, activeLoadV_ = 0;
  float learnedStartV_[2] = {0, 0}, learnedLoadV_[2] = {0, 0};
  uint8_t recoveries_ = 0;
  bool startAdaptationEnabled_ = true, loadAdaptationEnabled_ = true;
  bool learningInhibited_ = false, learningEligible_ = false, startCandidatePending_ = false;
  int slot() const { return direction_ > 0 ? 1 : 0; }
  void finish(State state, const char* reason = "");
  void settle(uint32_t now);
  void recover(uint32_t now);
  void handoff(uint32_t now, float speed);
  void apply(float effort, float dt, float slew);
  float feedforward(float speed) const;
};
#endif
