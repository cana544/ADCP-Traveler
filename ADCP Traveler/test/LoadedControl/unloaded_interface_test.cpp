#include "Arduino.h"
#include <cassert>
#include <iostream>
#include "control_diagnostics.h"
#include "encoder.cpp"
#include "motor_controller.cpp"
#include "distance_controller.cpp"
#include "section_plan.cpp"
#include "section_controller.cpp"
#include "control_diagnostics.cpp"

int main() {
  fakeUs = 3000000;
  Encoder encoder; MotorController motor; DistanceController controller(encoder,motor);
  ImuMonitor imu; ControlDiagnostics diagnostics;
  auto command = [&](const char* line) {
    Serial.input = line;
    bool stop = false;
    while (Serial.available()) stop = diagnostics.poll(controller,imu) || stop;
    if (stop) controller.cancel();
    return stop;
  };
  for (const char* bad : {"TEST MOVE 0 1\n", "TEST MOVE 501 1\n", "TEST MOVE nan 1\n", "TEST MOVE 10 0\n", "TEST MOVE 10 1 garbage\n"}) {
    command(bad); assert(!controller.isActive());
  }
  command("TEST MOVE 10 1\n");
  assert(controller.isActive());
  assert(controller.telemetry().targetCm == 10);
  command("TEST MOVE 50 -1\n");
  assert(controller.telemetry().targetCm == 10);
  fakeUs += 1000000; command("TEST PING\n");
  fakeUs += 1500000; assert(!diagnostics.poll(controller,imu));
  fakeUs += 500001; assert(diagnostics.poll(controller,imu));
  controller.cancel(); assert(!controller.isActive() && motor.currentSpeed() == 0);
  command("TEST PING\n"); assert(!controller.isActive());
  command("TEST MOVE 10 -1\n"); assert(controller.isActive());
  assert(command("STOP\n"));
  fakeUs += 3000000; diagnostics.poll(controller,imu);
  assert(!controller.isActive() && motor.currentSpeed() == 0);
  std::cout << "Temporary serial MOVE validation, busy rejection, heartbeat and STOP passed\n";
}
