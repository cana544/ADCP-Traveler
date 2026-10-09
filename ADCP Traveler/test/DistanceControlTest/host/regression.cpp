#include "Arduino.h"
#include <iostream>
#define private public
#include "../DistanceControlTest.ino"
#undef private

int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
  };
  MotionProfile profile;
  profile.start(50.0f, 1);
  check(fabsf(profile.sample(2.0f).velocityCmS - 20.0f) < 0.01f,
        "50 cm profile must reach the reduced common 20 cm/s limit");
  profile.start(10.0f, 1);
  check(fabsf(profile.sample(1.0f).velocityCmS - 10.0f) < 0.01f,
        "10 cm profile must use the common 10 cm/s^2 acceleration");
  fakeUs = 0;
  setup();
  check(!distanceController.isActive() && motorController.currentSpeed() == 0,
        "boot must remain idle until an explicit move command");
  // A wheel moving at 4.19 cm/s produces a pulse every 150 ms.
  Encoder slow;
  for (uint32_t t = 1000; t <= 751000; t += 150000) {
    fakeUs = t;
    slow.onPulse();
    slow.update();
  }
  fakeUs += 110000;
  slow.update();
  check(slow.velocityCmS() > 3.0f && !slow.isStopped(),
        "slow continuous motion must not report a false stop between pulses");
  fakeUs += 190000;
  slow.update();
  check(slow.velocityCmS() > 2.0f && slow.velocityCmS() < 2.2f,
        "late pulses must reduce the speed estimate during deceleration");
  fakeUs += 600000;
  slow.update();
  check(slow.velocityCmS() == 0 && slow.isStopped(),
        "stationary encoder must eventually report stopped");

  Encoder e;
  Encoder accelerating;
  for (uint32_t t : {1000U, 101000U, 201000U, 276000U, 351000U}) {
    fakeUs=t; accelerating.onPulse(); accelerating.update();
  }
  check(accelerating.velocityCmS() > 8 && accelerating.velocityCmS() < 9,
        "velocity estimator must converge after two new pulse intervals");
  Encoder noisy;
  fakeUs = 1000; noisy.onPulse(); noisy.update();
  fakeUs = 101000; noisy.onPulse(); noisy.update();
  fakeUs = 109000; noisy.onPulse(); noisy.update();
  fakeUs = 201000; noisy.onPulse(); noisy.update();
  check(noisy.signedPulses_ == 3 && noisy.velocityCmS() > 6 && noisy.velocityCmS() < 6.6f,
        "an 8 ms noise edge must not inflate pulse count or velocity at slow speed");
  Encoder moderatelyNoisy;
  fakeUs=1000; moderatelyNoisy.onPulse(); moderatelyNoisy.update();
  fakeUs=101000; moderatelyNoisy.onPulse(); moderatelyNoisy.update();
  fakeUs=122000; moderatelyNoisy.onPulse(); moderatelyNoisy.update();
  fakeUs=201000; moderatelyNoisy.onPulse(); moderatelyNoisy.update();
  check(moderatelyNoisy.signedPulses_ == 4 && moderatelyNoisy.velocityCmS() < 10,
        "an isolated short interval must be smoothed without discarding plausible pulses");
  Encoder abrupt;
  for (uint32_t t : {1000U, 101000U, 201000U, 251000U, 301000U}) {
    fakeUs=t; abrupt.onPulse(); abrupt.update();
  }
  check(abrupt.signedPulses_ == 5,
        "valid pulses after an abrupt speed change must not enter a count lockout");
  MotorController m;
  DistanceController c(e, m);
  for (float distance : {10.0f, 40.0f, 50.0f, 500.0f}) {
    profile.start(distance, 1);
    float integral = 0, previous = 0;
    const float dt = 0.001f;
    for (float t = dt; t <= profile.duration() + dt; t += dt) {
      const auto ref = profile.sample(t);
      check(ref.velocityCmS >= -0.001f && ref.velocityCmS <= 20.001f,
            "reference must obey velocity bounds");
      integral += (previous + ref.velocityCmS) * 0.5f * dt;
      previous = ref.velocityCmS;
    }
    check(fabsf(integral - distance) < 0.08f, "velocity area must equal commanded distance");
    check(profile.sample(profile.duration()).finished, "profile must finish at its analytical duration");
    check(fabsf(profile.sample(profile.duration()).positionCm - distance) < 0.001f,
          "reference must finish exactly at commanded distance");
  }
  profile.start(10, -1);
  check(profile.sample(1).velocityCmS == -10, "reverse reference must have correct sign");
  check(!handleCommand("MOVE nan 1"), "nonfinite moves must be rejected");
  check(!handleCommand("MOVE 9 1"), "moves below range must be rejected");
  check(!handleCommand("CONFIG 20 10 3 13 2 nan 2"), "nonfinite settings must be rejected");
  check(!handleCommand("MOVE 50 1 trailing"), "trailing garbage must be rejected");
  check(c.beginMove(50, 1), "valid stationary move must start");
  fakeUs += 2100000;
  c.update();
  check(c.state() == DistanceController::State::FAILED && m.currentSpeed() == 0,
        "lost heartbeat must stop the motor");
  check(c.beginMove(10, 1), "failed trial must permit an explicit new trial");
  const uint32_t start = fakeUs;
  for (int i = 1; i <= 70; ++i) {
    fakeUs = start + i * 50000;
    c.heartbeat(); c.update();
  }
  check(c.state() == DistanceController::State::FAILED && m.currentSpeed() == 0,
        "no-pulse move must end as failed without endless creeping");
  check(c.beginMove(50, 1), "next trial must still be possible");
  e.signedPulses_ = 80;
  fakeUs += 5000000;
  c.heartbeat(); c.update();
  check(m.currentSpeed() == 0, "profile finish must remove drive");
  fakeUs += 1100000;
  c.heartbeat(); c.update();
  check(c.state() == DistanceController::State::COMPLETE && m.currentSpeed() == 0,
        "stationary endpoint within one pulse must complete without restarting");
  Serial.output.clear(); fakeUs += 50000; c.update();
  check(Serial.output.find("COMPLETE") != std::string::npos,
        "telemetry must continue after the COMPLETE event");
  check(c.beginMove(10, -1), "reverse trial must be permitted after completion");
  fakeUs += 8000000;
  c.heartbeat(); c.update();
  check(c.state() == DistanceController::State::FAILED && m.currentSpeed() == 0,
        "trial deadline must stop even with a valid heartbeat");
  check(c.beginMove(10, 1), "timed-out move must allow a subsequent explicit trial");
  fakeUs += 500000;
  c.heartbeat(); c.update();
  c.stop();
  check(c.state() == DistanceController::State::FAILED && m.currentSpeed() == 0,
        "STOP must cancel active drive immediately");
  if (!failures) std::cout << "Distance control regression checks passed\n";
  return failures ? 1 : 0;
}
