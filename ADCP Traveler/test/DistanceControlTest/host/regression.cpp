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
  MotorController m;
  DistanceController c(e, m);
  c.phaseDirection_ = 1;
  check(c.applyMinimumDriveVoltage(1.5f, 5.0f, 2.0f) >=
            Config::Control::MIN_TRACKING_VOLTAGE,
        "low speed tracking must keep sufficient drive to avoid stalling");
  c.phaseDirection_ = -1;
  check(c.applyMinimumDriveVoltage(-1.5f, -5.0f, -2.0f) <=
            -Config::Control::MIN_TRACKING_VOLTAGE,
        "reverse tracking must keep sufficient drive");

  c.beginMove(50.0f, 1);
  c.profileTimingStarted_ = true;
  e.signedPulses_ = 80; // 50.265 cm: nearest encoder position to 50 cm.
  c.commandedVelocityCmS_ = 5.0f;
  c.previousEffortVoltage_ = 2.35f;
  c.runControlPhase();
  check(m.currentSpeed() == 0,
        "reaching target tolerance must remove drive without another kick");
  for (int i = 0; i < 5; ++i) c.runControlPhase();
  check(c.state_ == DistanceController::State::COMPLETE && m.currentSpeed() == 0,
        "target confirmation must complete without restarting the motor");
  if (!failures) std::cout << "Distance control regression checks passed\n";
  return failures ? 1 : 0;
}
