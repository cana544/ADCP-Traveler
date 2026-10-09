#include "Arduino.h"
#include <iostream>
#include <limits>

// Exercise the actual app sources against the frozen standalone controller.
#define private public
#include "../../../include/distance_controller.h"
namespace Validated {
#include "../DistanceControlTest.ino"
}
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
  for (float distance : {10.f, 20.f, 25.f, 30.f, 40.f, 50.f, 75.f, 100.f, 250.f, 500.f}) {
    for (int direction : {1, -1}) {
      MotionProfile actual;
      Validated::MotionProfile expected;
      actual.start(distance, direction); expected.start(distance, direction);
      check(fabsf(actual.durationSeconds() - expected.duration()) < .001f,
            "app profile timing must match validated profile");
      bool matches = true;
      for (float t = 0; t < expected.duration() + 1; t += .05f) {
        const auto a = actual.sample(t);
        const auto b = expected.sample(t);
        matches &= fabsf(a.positionCm - b.positionCm) < .001f &&
                   fabsf(a.velocityCmS - b.velocityCmS) < .001f && a.finished == b.finished;
      }
      check(matches, "app reference must match validated reference in both directions");
    }
  }
  for (float distance : {10.f, 20.f, 25.f, 30.f, 40.f, 50.f, 75.f, 100.f, 250.f, 500.f}) {
   for (int direction : {1, -1}) {
    fakeUs = 1000;
    Encoder e; MotorController m; DistanceController c(e, m);
    Validated::Encoder ve; Validated::MotorController vm;
    Validated::DistanceController vc(ve, vm);
    c.beginMove(distance, direction); vc.beginMove(distance, direction);
    bool matches = true;
    float lastPosition = 0;
    // Feed identical analytically timed edges to each real encoder.
    Validated::MotionProfile reference; reference.start(distance, direction);
    const uint32_t end = static_cast<uint32_t>((reference.duration() + 2.5f) * 1e6f);
    for (uint32_t t = 1000; t <= end; t += 1000) {
      fakeUs = t;
      float x = fabsf(reference.sample((t - 1000)*1e-6f).positionCm);
      if (x - lastPosition >= Config::Encoder::DISTANCE_PER_PULSE_CM) {
        e.onPulse(); ve.onPulse(); lastPosition += Config::Encoder::DISTANCE_PER_PULSE_CM;
      }
      if ((t - 1000) % 50000 == 0) {
        e.update(); ve.update(); vc.heartbeat(); c.update(); vc.update();
        matches &= direction * m.currentSpeed() >= 0 && abs(m.currentSpeed()) <= 255 &&
                   fabsf(e.velocityCmS() - ve.velocityCmS()) < .001f &&
                   fabsf(e.positionCm() - ve.positionCm()) < .001f;
      }
    }
    check(matches, "startup-aware app must preserve encoder estimates and bounded directional output");
    check(c.state() == DistanceController::State::COMPLETE && m.currentSpeed() == 0,
          "successful move must complete without a second move");
    c.cancel(); m.setSpeed(direction * 200);
    check(m.currentSpeed() == direction * 200 && m.isEnabled(),
          "manual control must remain available after cancellation");
   }
  }
  fakeUs = 1000;
  Encoder e; MotorController m; DistanceController c(e, m);
  c.beginMove(10, 1);
  fakeUs += 200000; e.onPulse(); e.update(); c.update();
  fakeUs += 200000; e.onPulse(); e.update(); c.update();
  fakeUs += 2100000; e.update(); c.update();
  check(m.currentSpeed() == 0, "profile end must stop PWM even when endpoint is missed");
  fakeUs += 1000000; c.update();
  check(!c.isActive() && std::string(c.statusText()) == "ERROR",
        "missed endpoint must terminate as an app error");
  c.beginMove(10, -1);
  fakeUs += 10100000; c.update();
  check(!c.isActive() && m.currentSpeed() == 0,
        "delayed update must enforce bounded trial deadline");
  c.cancel();
  c.beginMove(std::numeric_limits<float>::quiet_NaN(), 1);
  check(!c.isActive() && m.currentSpeed() == 0, "nonfinite move must not start");
  // Manual full-speed encoder counts must not acquire the distance debounce.
  Encoder fast;
  for (uint32_t t : {9000001U, 9008001U, 9016001U, 9024001U}) {
    fakeUs = t; fast.onPulse(); fast.update();
  }
  check(fast.pulseCount() == 4 && fast.velocityCmS() > 70,
        "manual high-speed pulse counts and measurement must be preserved");
  MotorController transitionMotor;
  DistanceController transition(fast, transitionMotor);
  fakeUs += 300000; fast.update();
  transition.beginMove(10, -1);
  check(!transition.isActive() && fast.direction_ == 1,
        "distance reversal must wait the validated 500 ms after manual coasting");
  fakeUs += 200000;
  transition.beginMove(10, -1);
  check(transition.isActive() && fast.direction_ == -1,
        "distance mode must become available after its full stopped interval");
  transition.cancel();
  const int32_t beforeManual = fast.pulseCount();
  for (int i = 0; i < 5; ++i) {
    fakeUs += 8000; fast.onPulse(); fast.update();
  }
  check(fast.pulseCount() == beforeManual - 5 && fabsf(fast.velocityCmS()) > 70,
        "cancellation must restore full-speed manual encoder behavior");
  std::cout << "App integration checks: " << failures << " failures\n";
  return failures ? 1 : 0;
}
