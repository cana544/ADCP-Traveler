#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include "config.h"
#include "section_controller.h"

uint32_t testTimeUs = 1000000;
void (*testPulse)() = nullptr;
SerialStub Serial;

struct Rig {
  Encoder encoder;
  MotorController motor;
  DistanceController distance{encoder, motor};
  SectionController sections{encoder, motor, distance};
  Rig() { encoder.begin(21); motor.begin(25, 26, 27, 14); }
  void tick(uint32_t us = 50000) {
    testTimeUs += us; encoder.update(); distance.update(); sections.update();
  }
  void pulses(int count) {
    for (int i = 0; i < count; ++i) { testTimeUs += 50000; testPulse(); }
    encoder.update(); sections.update();
  }
  void scan(int direction = 1, int count = 1000) {
    assert(sections.startScan(direction)); tick(); pulses(count);
    sections.stop(); assert(motor.currentSpeed() == 0);
    tick(600000);
  }
  void finishMove() {
    // Feed the real encoder the distance still commanded by the real controller.
    const float remaining = distance.commandDistanceCm();
    const int count = static_cast<int>(std::round(remaining / Config::Encoder::DISTANCE_PER_PULSE_CM));
    for (int i = 0; i < count; ++i) {
      testTimeUs += 50000; testPulse(); encoder.update(); distance.update(); sections.update();
    }
    for (int i = 0; i < 140 && distance.isActive(); ++i) tick();
    assert(distance.state() == DistanceController::State::COMPLETE);
  }
};

void planner() {
  SectionPlan plan;
  assert(plan.generate(760, 6));
  assert(std::fabs(plan.widthsCm[0] - 180) < .001f);
  assert(std::fabs(plan.widthsCm[2] - 100) < .001f);
  assert(std::fabs(plan.midpointsCm[0] - 90) < .001f);
  for (int n = 6; n <= 60; ++n) {
    assert(plan.generate(2480, n));
    float sum = 0;
    for (int i = 0; i < n; ++i) {
      sum += plan.percentages[i];
      assert(std::fabs(plan.widthsCm[i] - plan.widthsCm[n-1-i]) < .001f);
      assert(plan.widthsCm[i] >= 10);
      assert(plan.boundariesCm[i+1] > plan.boundariesCm[i]);
    }
    assert(std::fabs(sum - 100) < .001f);
    assert(std::fabs(plan.boundariesCm[n] - 2480) < .005f);
    assert(std::fabs(plan.weights[0] - 1.8f) < .00001f);
    assert(std::fabs(plan.weights[n/2] - 1) < .00001f);
  }
  assert(!plan.generate(75.9f, 6));
  assert(plan.count == 0);
  assert(plan.generate(76, 6));
  assert(!plan.generate(1000, 5));
  assert(!plan.generate(1000, 61));
  assert(!plan.generate(0, 20));
  assert(!plan.generate(std::numeric_limits<float>::infinity(), 20));
  assert(SectionPlan::maxValidCount(75.9f) == 0);
  assert(SectionPlan::maxValidCount(76) == 6);
  assert(SectionPlan::maxValidCount(2480) == 60);
  std::puts("PASS: exact model, symmetry, normalization, boundaries, min width and limits");
}

void scanAndRamp() {
  for (int direction : {-1, 1}) {
    Rig rig;
    rig.motor.setEnabled(false);
    assert(!rig.sections.startScan(direction));
    rig.motor.setEnabled(true);
    assert(!rig.sections.startScan(0));
    assert(rig.sections.startScan(direction));
    assert(rig.motor.currentSpeed() == 0);
    int previous = 0;
    for (int i = 0; i < 80; ++i) {
      rig.tick();
      const int speed = direction * rig.motor.currentSpeed();
      assert(speed >= previous && speed <= 255); previous = speed;
      if (i == 19) assert(speed > 0 && speed < 100);
    }
    assert(rig.motor.currentSpeed() == direction * 255);
    rig.pulses(100);
    rig.sections.stop();
    assert(rig.motor.currentSpeed() == 0);
    assert(!rig.sections.configure(6)); // still coasting
    rig.pulses(3); rig.tick(600000);
    assert(std::fabs(rig.sections.spanCm() - 103 * Config::Encoder::DISTANCE_PER_PULSE_CM) < .001f);
    assert(!rig.sections.configure(6)); // span too short
    assert(!rig.sections.go());
  }
  std::puts("PASS: signed smooth ramp to full PWM, immediate STOP, scan coast and short span");
}

void returnAndResume() {
  for (int scanDirection : {-1, 1}) {
    Rig rig;
    assert(!rig.sections.configure(20));
    assert(!rig.sections.go());
    rig.scan(scanDirection);
    assert(!rig.sections.configure(6.5));
    assert(!rig.sections.configure(std::numeric_limits<double>::quiet_NaN()));
    assert(rig.sections.configure(6));
    assert(rig.sections.nextSection() == (scanDirection == 1 ? 6 : 1));
    assert(rig.sections.go());
    assert(!rig.sections.go());
    assert(!rig.sections.configure(10));
    rig.pulses(10);
    rig.sections.stop();
    assert(rig.motor.currentSpeed() == 0);
    assert(rig.sections.completedCount() == 0);
    rig.pulses(2); rig.tick(600000);
    const float fullWidth = rig.sections.plan().widthsCm[rig.sections.nextSection()-1];
    const float remaining = rig.sections.remainingCm();
    assert(std::fabs(remaining - (fullWidth - 12 * Config::Encoder::DISTANCE_PER_PULSE_CM)) < .002f);
    assert(rig.sections.go());
    assert(std::fabs(rig.distance.commandDistanceCm() - remaining) < .002f);
    rig.finishMove();
    assert(rig.sections.completedCount() == 1);
    assert(!rig.distance.isActive());
    for (int i = 1; i < 6; ++i) {
      assert(rig.sections.nextSection() == (scanDirection == 1 ? 6-i : i+1));
      assert(rig.sections.go()); rig.finishMove();
      assert(rig.sections.completedCount() == i+1);
    }
    assert(rig.sections.stage() == SectionController::Stage::FINISHED);
    assert(!rig.sections.go());
    assert(std::fabs(rig.encoder.positionCm()) <= Config::Control::POSITION_TOLERANCE_CM);
    assert(rig.sections.newScan());
    assert(rig.sections.plan().count == 0);
  }
  std::puts("PASS: both return orders, no skipping, coast-aware resume, absolute targets and finish");
}

void failureAndInvalidation() {
  Rig rig;
  rig.scan(); assert(rig.sections.configure(20));
  assert(!rig.sections.configure(60)); // too many for this span
  assert(!rig.sections.go());
  assert(rig.sections.configure(6)); assert(rig.sections.go());
  assert(!rig.sections.newScan());
  rig.tick(Config::Control::STARTUP_TIMEOUT_US + 1);
  assert(rig.sections.completedCount() == 0);
  assert(rig.sections.stage() == SectionController::Stage::READY_FOR_SECTION);
  rig.motor.setEnabled(true); assert(rig.sections.go());
  rig.sections.stop(); rig.tick(600000);
  rig.sections.invalidate();
  assert(!rig.sections.go()); assert(rig.motor.currentSpeed() == 0);
  std::puts("PASS: invalid count revokes plan, startup failure preserves section, invalidation stops");
}

void powerOffAndLongScan() {
  Rig rig;
  assert(rig.sections.startScan(1)); rig.tick(4000000);
  assert(rig.motor.currentSpeed() == 255);
  // More than one micros() wrap must never restart a completed scan ramp.
  rig.tick(0xFFFFFFFFUL - 4000000UL + 1001UL);
  assert(rig.motor.currentSpeed() == 255);
  rig.pulses(1000);
  rig.motor.setEnabled(false); rig.sections.update();
  assert(rig.motor.currentSpeed() == 0);
  rig.tick(600000);
  assert(rig.sections.stage() == SectionController::Stage::CONFIGURE);
  assert(rig.sections.configure(6)); assert(rig.sections.go());
  rig.pulses(10);
  rig.sections.stop(); rig.motor.setEnabled(false);
  rig.tick(600000);
  assert(rig.sections.completedCount() == 0);
  assert(rig.sections.go());
  assert(rig.motor.isEnabled());
  std::puts("PASS: full-speed scan survives timer wrap; power-off pauses and GO resumes");
}

int main() { planner(); scanAndRamp(); returnAndResume(); failureAndInvalidation(); powerOffAndLongScan(); }
