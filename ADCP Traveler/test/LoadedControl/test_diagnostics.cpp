#include "Arduino.h"
#include <ArduinoJson.h>
#include <cassert>
#include <iostream>
#include "control_diagnostics.h"
#include "../../src/encoder.cpp"
#include "../../src/motor_controller.cpp"
#include "../../src/distance_controller.cpp"
#include "../../src/section_plan.cpp"
#include "../../src/section_controller.cpp"
#include "../../src/control_diagnostics.cpp"

int main() {
  fakeUs = 1000;
  Encoder encoder; MotorController motor; DistanceController controller(encoder, motor);
  SectionController sections(encoder, motor, controller); ImuMonitor imu; ControlDiagnostics diagnostics;
  Serial.input = "LOG ON\nADAPT START OFF\n";
  assert(!diagnostics.poll(controller, imu));
  assert(!controller.startAdaptationEnabled() && controller.loadAdaptationEnabled());
  Serial.input = "ADAPT LOAD OFF\n";
  assert(!diagnostics.poll(controller, imu));
  assert(!controller.startAdaptationEnabled() && !controller.loadAdaptationEnabled());
  motor.setSpeed(128); // manual control, independent of the distance controller
  Serial.output.clear(); fakeUs += 200000;
  diagnostics.emit(42, controller, encoder, motor, sections, imu);
  assert(Serial.output.rfind("GG,", 0) == 0);
  StaticJsonDocument<4096> doc;
  assert(!deserializeJson(doc, Serial.output.c_str() + 3));
  assert(fabsf(doc["applied_v"].as<float>() - 6.02353f) < .001f);
  assert(doc["mode"] == "MANUAL");
  Serial.room = 0; fakeUs += 200000;
  const auto previous = Serial.output;
  diagnostics.emit(42, controller, encoder, motor, sections, imu);
  assert(Serial.output == previous);
  Serial.room = 4096; Serial.output.clear(); fakeUs += 200000;
  diagnostics.emit(42, controller, encoder, motor, sections, imu);
  assert(!deserializeJson(doc, Serial.output.c_str() + 3));
  assert(doc["drops"] == 1);
  motor.stop(); controller.beginMove(100, 1);
  Serial.output.clear(); fakeUs += 200000;
  diagnostics.emit(42, controller, encoder, motor, sections, imu);
  assert(!deserializeJson(doc, Serial.output.c_str() + 3));
  assert(doc["mode"] == "DISTANCE" && doc["transition_reason"] == "GO");
  Serial.input = "ADAPT ON\nSTOP\n";
  assert(diagnostics.poll(controller, imu));
  assert(!controller.startAdaptationEnabled() && !controller.loadAdaptationEnabled());
  controller.cancel(); // same cancellation invoked by the app's STOP handler
  assert(!controller.isActive() && motor.currentSpeed() == 0);
  std::cout << "Real diagnostic serialization, independent commands, busy/STOP and TX drop checks passed\n";
}
