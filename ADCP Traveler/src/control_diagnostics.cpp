#include "control_diagnostics.h"
#include <ArduinoJson.h>
#include "config.h"

void ControlDiagnostics::acknowledge(const char* message) {
  const size_t n = strlen(message);
  if (Serial.availableForWrite() >= static_cast<int>(n)) Serial.write(reinterpret_cast<const uint8_t*>(message), n);
}

bool ControlDiagnostics::poll(DistanceController& controller, ImuMonitor& imu) {
  for (int budget = 0; budget < 32 && Serial.available() > 0; ++budget) {
    const auto command = parser_.feed(static_cast<char>(Serial.read()));
    switch (command) {
      case DiagnosticCommands::NONE: break;
      case DiagnosticCommands::STOP: acknowledge("GG_ACK,STOP\n"); return true;
      case DiagnosticCommands::LOG_ON: enabled_ = true; acknowledge("GG_ACK,LOG_ON\n"); break;
      case DiagnosticCommands::LOG_OFF: enabled_ = false; acknowledge("GG_ACK,LOG_OFF\n"); break;
      case DiagnosticCommands::IMU_CHECK:
        if (controller.isActive()) acknowledge("GG_ACK,BUSY\n");
        else { imu.startQualityCheck(); acknowledge("GG_ACK,IMU_CHECK\n"); }
        break;
      case DiagnosticCommands::ADAPT_ON:
      case DiagnosticCommands::ADAPT_OFF:
      case DiagnosticCommands::ADAPT_RESET:
      case DiagnosticCommands::ADAPT_START_ON:
      case DiagnosticCommands::ADAPT_START_OFF:
      case DiagnosticCommands::ADAPT_LOAD_ON:
      case DiagnosticCommands::ADAPT_LOAD_OFF:
        if (controller.isActive()) { acknowledge("GG_ACK,BUSY\n"); break; }
        if (command == DiagnosticCommands::ADAPT_RESET) controller.resetLearning();
        else if (command == DiagnosticCommands::ADAPT_START_ON || command == DiagnosticCommands::ADAPT_START_OFF)
          controller.setAdaptationEnabled(command == DiagnosticCommands::ADAPT_START_ON, controller.loadAdaptationEnabled());
        else if (command == DiagnosticCommands::ADAPT_LOAD_ON || command == DiagnosticCommands::ADAPT_LOAD_OFF)
          controller.setAdaptationEnabled(controller.startAdaptationEnabled(), command == DiagnosticCommands::ADAPT_LOAD_ON);
        else controller.setAdaptationEnabled(command == DiagnosticCommands::ADAPT_ON);
        acknowledge("GG_ACK,ADAPT_UPDATED\n"); break;
      default: acknowledge("GG_ACK,INVALID_COMMAND\n"); break;
    }
  }
  return false;
}

void ControlDiagnostics::emit(uint32_t boot, const DistanceController& controller, const Encoder& encoder,
    const MotorController& motor, const SectionController& sections, const ImuMonitor& imu) {
  const uint32_t now = micros();
  // Full IMU snapshots are intentionally downsampled to 5 Hz at USB 115200 baud.
  if (!enabled_ || now - lastSampleUs_ < 200000UL) return;
  lastSampleUs_ = now; ++sequence_;
  const auto t = controller.telemetry();
  const auto& data = imu.snapshot();
  // Single owner under the control mutex; avoid consuming the ESP32 loop stack.
  static StaticJsonDocument<3072> doc;
  doc.clear();
  doc["version"] = 1; doc["boot"] = boot; doc["us"] = now; doc["seq"] = sequence_;
  doc["move"] = t.moveId; doc["phase"] = controller.phaseText(); doc["failure"] = controller.failureText();
  doc["transition_reason"] = controller.transitionReason();
  doc["braking_limited"] = controller.brakingLimited();
  doc["mode"] = sections.stage() == SectionController::Stage::SCANNING ? "SCAN" :
      sections.stage() == SectionController::Stage::MOVING_SECTION ? "SECTION" :
      controller.isActive() ? "DISTANCE" : motor.currentSpeed() != 0 ? "MANUAL" : "IDLE";
  doc["section_stage"] = sections.stageText(); doc["position_cm"] = encoder.positionCm();
  doc["start_cm"] = controller.moveStartPositionCm(); doc["target_cm"] = t.targetCm;
  doc["remaining_cm"] = t.remainingCm; doc["velocity_cm_s"] = encoder.velocityCmS();
  doc["command_cm_s"] = t.commandCmS; doc["requested_v"] = t.requestedV;
  doc["applied_v"] = motor.currentSpeed() * Config::Control::SUPPLY_VOLTAGE / 255;
  doc["pwm"] = motor.currentSpeed();
  doc["integral_v"] = t.integralV; doc["load_v"] = t.loadV; doc["learned_start_v"] = t.startV;
  doc["adapt"] = t.adaptationEnabled; doc["learning"] = t.learningEligible;
  doc["adapt_start"] = t.startAdaptationEnabled; doc["adapt_load"] = t.loadAdaptationEnabled;
  doc["elapsed_us"] = t.elapsedUs; doc["deadline_us"] = t.deadlineUs;
  doc["progress_age_us"] = t.progressAgeUs; doc["recoveries"] = t.recoveries;
  doc["pulses"] = encoder.pulseCount(); doc["pulse_age_us"] = encoder.pulseAgeUs();
  doc["drops"] = drops_;
  JsonObject sensor = doc.createNestedObject("imu");
  sensor["healthy"] = data.healthy(now); sensor["resets"] = data.resets;
  sensor["errors"] = data.errors; sensor["transport_errors"] = imu.transportErrors();
  sensor["quality_ready"] = data.qualityReady; sensor["quality_pass"] = data.qualityPass;
  sensor["quality_samples"] = data.qualitySamples; sensor["quality_norm"] = data.qualityMeanNorm;
  sensor["quality_gyro"] = data.qualityMeanGyro;
  const char* names[] = {"accel", "linear", "gyro", "quaternion"};
  for (int i = 0; i < ImuData::COUNT; ++i) {
    JsonObject report = sensor.createNestedObject(names[i]);
    report["valid"] = data.valid[i]; report["age_us"] = data.age(static_cast<ImuData::Report>(i), now);
    report["sensor_us"] = data.sensorUs[i]; report["accuracy"] = data.accuracy[i];
    JsonArray vector = report.createNestedArray("v");
    for (int axis = 0; axis < (i == ImuData::ORIENTATION ? 4 : 3); ++axis) vector.add(data.values[i][axis]);
  }
  static char line[3072];
  const size_t needed = measureJson(doc);
  if (doc.overflowed() || needed + 5 > sizeof(line) || Serial.availableForWrite() < static_cast<int>(needed + 4)) {
    ++drops_; return;
  }
  memcpy(line, "GG,", 3);
  const size_t n = serializeJson(doc, line + 3, sizeof(line) - 4);
  line[n + 3] = '\n'; Serial.write(reinterpret_cast<const uint8_t*>(line), n + 4);
}
