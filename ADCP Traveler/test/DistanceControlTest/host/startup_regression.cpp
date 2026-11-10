#include "Arduino.h"
#include <iostream>
#include "../../../include/distance_controller.h"
#include "../../../src/encoder.cpp"
#include "../../../src/motor_controller.cpp"
#include "../../../src/distance_controller.cpp"
int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* message) { if (!ok) { ++failures; std::cerr << message << '\n'; } };
  for (int direction : {-1, 1}) {
    fakeUs = 1000; Encoder encoder; MotorController motor; DistanceController controller(encoder, motor);
    controller.beginMove(5, direction);
    for (int i = 0; i < 60; ++i) { fakeUs += 50000; encoder.update(); controller.update(); }
    check(controller.state() == DistanceController::State::STARTING,
          "three-second delayed breakaway must not consume a short-move trajectory");
    check(direction * motor.currentSpeed() > 0 && abs(motor.currentSpeed()) <= 170,
          "startup must be bounded and directional");
    controller.cancel();
    for (int i = 0; i < 205; ++i) { fakeUs += 50000; encoder.update(); controller.update(); }
    check(motor.currentSpeed() == 0 && controller.state() == DistanceController::State::CANCELLED,
          "cancelled startup must not resume");
    controller.beginMove(5, direction);
    for (int i = 0; i < 205; ++i) { fakeUs += 50000; encoder.update(); controller.update(); }
    check(controller.state() == DistanceController::State::FAILED && motor.currentSpeed() == 0,
          "blocked startup must fail within ten seconds");
    controller.beginMove(5, direction); fakeUs += 300000; controller.update();
    check(controller.state() == DistanceController::State::FAILED && motor.currentSpeed() == 0,
          "a delayed active control loop must stop instead of catching up");
  }
  std::cout << "Online startup and cancellation checks: " << failures << " failures\n";
  return failures ? 1 : 0;
}
