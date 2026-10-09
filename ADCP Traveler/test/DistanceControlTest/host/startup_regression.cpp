#include "Arduino.h"
#include <iostream>
#define private public
#include "../../../include/distance_controller.h"
#undef private
#include "../../../src/motion_profile.cpp"
#include "../../../src/encoder.cpp"
#include "../../../src/motor_controller.cpp"
#include "../../../src/distance_controller.cpp"

int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
  };
  for (int direction : {1, -1}) {
    fakeUs = 1000;
    Encoder e; MotorController m; DistanceController c(e, m);
    c.beginMove(5, direction);
    int previous = 0;
    // A short move must still be attempting startup after its old 1.4 s profile.
    for (int i = 0; i < 60; ++i) {
      fakeUs += 50000; e.update(); c.update();
      const int duty = direction * m.currentSpeed();
      check(duty >= previous && duty - previous <= 2, "startup effort must ramp gradually in commanded direction");
      check(duty <= 170, "startup effort must stay below 8 V on a 12 V supply");
      previous = duty;
    }
    check(c.isActive() && previous > 0, "delayed breakaway must not consume the motion profile");
    fakeUs += 100000; e.onPulse(); e.update(); c.update();
    check(c.state() == DistanceController::State::STARTING,
          "a single pulse must not confirm startup movement");
    fakeUs += 200000; e.onPulse(); e.update(); c.update();
    check(c.state() == DistanceController::State::MOVING,
          "two encoder pulses must enter profile control");
    const auto entry = c.profile_.sample(c.profileTimeOffsetS_);
    check(fabsf(entry.velocityCmS - direction * 3.141593f) < .001f,
          "profile entry must match the two-pulse measured velocity");
    const auto endpoint = c.profile_.sample(c.profile_.durationSeconds());
    check(fabsf(endpoint.positionCm + c.profilePositionOffsetCm_ - direction * 5.f) < .001f,
          "handoff profile must retain exactly the original endpoint");
    const float beforeHandoff = e.positionCm();
    const int handoffDuty = m.currentSpeed();
    fakeUs += 50000; e.update(); c.update();
    check(abs(m.currentSpeed() - handoffDuty) <= 27, "handoff output must respect the existing slew limit");
    // Eight total pulses = 5.03 cm; startup pulses must count toward that endpoint.
    for (int i = 0; i < 6; ++i) {
      fakeUs += 150000; e.onPulse(); e.update(); c.update();
    }
    check(fabsf(beforeHandoff) > 1.2f, "startup travel must be observable");
    check(m.currentSpeed() == 0, "reaching the original endpoint must stop PWM without adding startup distance");
    fakeUs += 1100000; e.update(); c.update();
    check(c.state() == DistanceController::State::COMPLETE, "original 5 cm endpoint must complete in both directions");
  }
  fakeUs = 1000;
  Encoder e; MotorController m; DistanceController c(e, m);
  c.beginMove(5, 1);
  for (int i = 0; i < 205; ++i) {
    fakeUs += 50000; e.update(); c.update();
    check(m.currentSpeed() <= 170, "blocked startup must never exceed effort limit");
  }
  check(!c.isActive() && c.state() == DistanceController::State::FAILED && m.currentSpeed() == 0,
        "blocked startup must fail and remove effort within 10 seconds");
  c.beginMove(5, -1);
  fakeUs += 50000; c.update(); c.cancel();
  fakeUs += 50000; c.update();
  check(!c.isActive() && !m.isEnabled() && m.currentSpeed() == 0,
        "cancelling startup must stop and disable the motor");
  c.beginMove(5, 1);
  fakeUs += 11000000; c.update();
  check(c.state() == DistanceController::State::FAILED && m.currentSpeed() == 0,
        "a delayed control update must enforce startup timeout before applying effort");
  for (int direction : {1, -1}) {
    fakeUs = 1000;
    Encoder fast; MotorController motor; DistanceController controller(fast, motor);
    controller.beginMove(5, direction);
    fakeUs += 50000; fast.onPulse(); fast.update(); controller.update();
    fakeUs += 50000; fast.onPulse(); fast.update(); controller.update();
    check(controller.state() == DistanceController::State::SETTLING && motor.currentSpeed() == 0,
          "breakaway speed requiring more than the remaining braking distance must stop immediately");
    fakeUs += 1100000; fast.update(); controller.update();
    check(controller.state() == DistanceController::State::FAILED,
          "stopping short for braking must report an error rather than claim completion or restart");
  }
  fakeUs = 1000;
  Encoder repeated; MotorController repeatedMotor; DistanceController repeatedMove(repeated, repeatedMotor);
  repeated.onPulse();
  fakeUs += 5000000; repeated.update();
  const float origin = repeated.positionCm();
  repeatedMove.beginMove(5, -1);
  fakeUs += 200000; repeated.onPulse(); repeated.update(); repeatedMove.update();
  fakeUs += 200000; repeated.onPulse(); repeated.update(); repeatedMove.update();
  const auto repeatedEntry = repeatedMove.profile_.sample(repeatedMove.profileTimeOffsetS_);
  check(fabsf(repeatedEntry.velocityCmS + 3.141593f) < .001f,
        "handoff velocity must exclude idle time since the preceding move");
  const auto repeatedEnd = repeatedMove.profile_.sample(repeatedMove.profile_.durationSeconds());
  check(fabsf(repeatedEnd.positionCm + repeatedMove.profilePositionOffsetCm_ + origin - (origin - 5)) < .001f,
        "repeated reversed moves must preserve endpoint from a nonzero origin");
  fakeUs = 1000;
  Encoder late; MotorController lateMotor; DistanceController lateMove(late, lateMotor);
  lateMove.beginMove(5, 1);
  for (int i = 0; i < 170; ++i) {
    fakeUs += 50000; late.update(); lateMove.update();
  }
  for (int i = 0; i < 8; ++i) {
    fakeUs += 50000; late.onPulse(); late.update(); lateMove.update();
  }
  fakeUs += 1100000; late.update(); lateMove.update();
  check(lateMove.state() == DistanceController::State::COMPLETE,
        "late startup followed by braking and coasting to target must get its full settling interval");
  std::cout << "Startup checks: " << failures << " failures\n";
  return failures ? 1 : 0;
}
