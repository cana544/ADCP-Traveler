#ifndef SECTION_CONTROLLER_H
#define SECTION_CONTROLLER_H

#include "distance_controller.h"
#include "section_plan.h"

class SectionController {
 public:
  enum class Stage { PRE_SCAN, SCANNING, CONFIGURE, READY_FOR_SECTION, MOVING_SECTION, FINISHED };
  SectionController(Encoder& encoder, MotorController& motor, DistanceController& distance);
  bool startScan(int direction);
  void stop();
  bool configure(double count);
  bool go();
  bool newScan();
  void invalidate();
  void update();

  Stage stage() const { return stage_; }
  const char* stageText() const;
  const char* stateText() const;
  const char* error() const { return error_; }
  const SectionPlan& plan() const { return plan_; }
  float spanCm() const;
  float remainingCm() const;
  int scanDirection() const { return scanDirection_; }
  int completedCount() const { return completed_; }
  int nextSection() const;
  int lastCompletedSection() const;
  bool active() const { return stage_ == Stage::SCANNING || stage_ == Stage::MOVING_SECTION; }
  bool locked() const { return executionStarted_; }
  bool stationary() const;

 private:
  Encoder& encoder_;
  MotorController& motor_;
  DistanceController& distance_;
  SectionPlan plan_;
  Stage stage_ = Stage::PRE_SCAN;
  int scanDirection_ = 1;
  int completed_ = 0;
  float scanStartCm_ = 0;
  float scanEndCm_ = 0;
  uint32_t scanStartUs_ = 0;
  bool scanSettling_ = false;
  bool executionStarted_ = false;
  const char* error_ = "";

  bool reject(const char* message);
  float targetCm() const;
  void completeSection();
};

#endif
