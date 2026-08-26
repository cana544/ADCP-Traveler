#include "motion_profile.h"

#include <Arduino.h>

#include "config.h"

MotionProfile::MotionProfile()
    : distanceCm_(0.0f),
      direction_(1),
      tAccel_(0.0f),
      tCruise_(0.0f),
      tTotal_(0.0f),
      vPeakCmS_(0.0f),
      xAccelCm_(0.0f),
      xCruiseCm_(0.0f),
      triangular_(true) {}

void MotionProfile::start(float distanceCm, int direction) {
  distanceCm_ = max(0.0f, distanceCm);
  direction_ = direction >= 0 ? 1 : -1;

  const float vMax = Config::Motion::V_MAX_CM_S;
  const float aMax = Config::Motion::A_MAX_CM_S2;
  const float dCrit = (vMax * vMax) / aMax;

  triangular_ = distanceCm_ <= dCrit;

  if (distanceCm_ <= 0.0f) {
    tAccel_ = 0.0f;
    tCruise_ = 0.0f;
    tTotal_ = 0.0f;
    vPeakCmS_ = 0.0f;
    xAccelCm_ = 0.0f;
    xCruiseCm_ = 0.0f;
    return;
  }

  if (triangular_) {
    tAccel_ = sqrtf(distanceCm_ / aMax);
    tCruise_ = 0.0f;
    tTotal_ = 2.0f * tAccel_;
    vPeakCmS_ = aMax * tAccel_;
    xAccelCm_ = 0.5f * aMax * tAccel_ * tAccel_;
    xCruiseCm_ = 0.0f;
  } else {
    tAccel_ = vMax / aMax;
    xAccelCm_ = 0.5f * aMax * tAccel_ * tAccel_;
    xCruiseCm_ = distanceCm_ - 2.0f * xAccelCm_;
    tCruise_ = xCruiseCm_ / vMax;
    tTotal_ = 2.0f * tAccel_ + tCruise_;
    vPeakCmS_ = vMax;
  }
}

MotionReference MotionProfile::sample(float elapsedSeconds) const {
  if (distanceCm_ <= 0.0f || elapsedSeconds <= 0.0f) {
    return {0.0f, 0.0f, distanceCm_ <= 0.0f};
  }

  const float aMax = Config::Motion::A_MAX_CM_S2;
  float position = 0.0f;
  float velocity = 0.0f;
  bool finished = false;

  if (triangular_) {
    if (elapsedSeconds < tAccel_) {
      velocity = aMax * elapsedSeconds;
      position = 0.5f * aMax * elapsedSeconds * elapsedSeconds;
    } else if (elapsedSeconds < tTotal_) {
      const float tau = elapsedSeconds - tAccel_;
      velocity = vPeakCmS_ - aMax * tau;
      position = xAccelCm_ + vPeakCmS_ * tau - 0.5f * aMax * tau * tau;
    } else {
      position = distanceCm_;
      velocity = 0.0f;
      finished = true;
    }
  } else {
    const float tCruiseEnd = tAccel_ + tCruise_;
    if (elapsedSeconds < tAccel_) {
      velocity = aMax * elapsedSeconds;
      position = 0.5f * aMax * elapsedSeconds * elapsedSeconds;
    } else if (elapsedSeconds < tCruiseEnd) {
      const float tau = elapsedSeconds - tAccel_;
      velocity = Config::Motion::V_MAX_CM_S;
      position = xAccelCm_ + Config::Motion::V_MAX_CM_S * tau;
    } else if (elapsedSeconds < tTotal_) {
      const float tau = elapsedSeconds - tCruiseEnd;
      velocity = Config::Motion::V_MAX_CM_S - aMax * tau;
      position = xAccelCm_ + xCruiseCm_ + Config::Motion::V_MAX_CM_S * tau -
                 0.5f * aMax * tau * tau;
    } else {
      position = distanceCm_;
      velocity = 0.0f;
      finished = true;
    }
  }

  return {direction_ * position, direction_ * velocity, finished};
}

float MotionProfile::durationSeconds() const { return tTotal_; }

float MotionProfile::signedDistanceCm() const {
  return direction_ * distanceCm_;
}
