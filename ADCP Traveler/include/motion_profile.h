#ifndef MOTION_PROFILE_H
#define MOTION_PROFILE_H

struct MotionReference {
  float positionCm;
  float velocityCmS;
  bool finished;
};

class MotionProfile {
 public:
  MotionProfile();

  void start(float distanceCm, int direction);
  MotionReference sample(float elapsedSeconds) const;
  float durationSeconds() const;
  float signedDistanceCm() const;

 private:
  float distanceCm_;
  int direction_;
  float tAccel_;
  float tCruise_;
  float tTotal_;
  float vPeakCmS_;
  float xAccelCm_;
  float xCruiseCm_;
  bool triangular_;
};

#endif
