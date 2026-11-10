#pragma once
#include "diagnostic_commands.h"
#include "distance_controller.h"
#include "section_controller.h"
#include "imu_monitor.h"

class ControlDiagnostics {
 public:
  // Returns true for an operator STOP; caller cancels the full section workflow.
  bool poll(DistanceController&, ImuMonitor&);
  void emit(uint32_t boot, const DistanceController&, const Encoder&,
            const MotorController&, const SectionController&, const ImuMonitor&);
 private:
  DiagnosticCommands parser_;
  bool enabled_ = false;
  uint32_t lastSampleUs_ = 0, drops_ = 0, sequence_ = 0;
  void acknowledge(const char* message);
};
