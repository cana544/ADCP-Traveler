#include "Arduino.h"
#include <iostream>
#include <limits>
#define private public
#include "distance_controller.h"
#undef private
#include "../../src/motion_profile.cpp"
#include "../../src/encoder.cpp"
#include "../../src/motor_controller.cpp"
#include "../../src/distance_controller.cpp"

int failures = 0;
void check(bool ok, const char* message) {
  if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

void symmetricEndpointBand() {
  check(Config::Control::POSITION_TOLERANCE_CM == .5f, "production tolerance must be 0.5 cm");
  for (int direction : {-1,1}) {
    for (float remaining : {-.5f,-.49f,.49f,.5f}) {
      fakeUs=1000; Encoder encoder; MotorController motor; DistanceController control(encoder,motor);
      control.beginMove(Config::Encoder::DISTANCE_PER_PULSE_CM+remaining,direction);
      encoder.setDirection(direction);
      fakeUs+=50000; encoder.onPulse();encoder.update();control.update();
      for (int i=0;i<50 && control.isActive();++i) {fakeUs+=50000;encoder.update();control.update();}
      check(control.state()==DistanceController::State::COMPLETE && motor.currentSpeed()==0,
            "stationary endpoints at either boundary of +/-0.5 cm must complete");
    }
    fakeUs=1000; Encoder encoder; MotorController motor; DistanceController control(encoder,motor);
    control.beginMove(Config::Encoder::DISTANCE_PER_PULSE_CM+.501f,direction);
    fakeUs+=50000;encoder.onPulse();encoder.update();control.update();
    check(control.isActive() && control.state()!=DistanceController::State::SETTLING,
          "0.501 cm short must not be accepted");
    control.cancel();
    fakeUs+=600000;
    control.beginMove(Config::Encoder::DISTANCE_PER_PULSE_CM-.501f,direction);
    fakeUs+=50000;encoder.onPulse();encoder.update();control.update();
    check(control.state()==DistanceController::State::FAILED && motor.currentSpeed()==0 &&
          std::string(control.failureText())=="OVERRUN", "0.501 cm beyond must fault without reversing");

    fakeUs=1000; Encoder coastEncoder; MotorController coastMotor; DistanceController coast(coastEncoder,coastMotor);
    coast.beginMove(Config::Encoder::DISTANCE_PER_PULSE_CM+.49f,direction);
    fakeUs+=50000;coastEncoder.onPulse();coastEncoder.update();coast.update();
    fakeUs+=50000;coastEncoder.onPulse();coastEncoder.update();coast.update();
    check(coast.state()==DistanceController::State::SETTLING && coastMotor.currentSpeed()==0,
          "coast across target inside the band must retain settling with zero output");
    fakeUs+=50000;coastEncoder.onPulse();coastEncoder.update();coast.update();
    check(coast.state()==DistanceController::State::FAILED && std::string(coast.failureText())=="OVERRUN",
          "coast beyond the symmetric band must invalidate settling");
  }
}

void terminalRoomOutsideBand() {
  for (int direction : {-1,1}) {
    fakeUs=1000; Encoder encoder; MotorController motor; DistanceController control(encoder,motor);
    control.beginMove(.52f,direction);
    control.state_=DistanceController::State::APPROACHING;
    fakeUs+=50000;encoder.update();control.update();
    check(control.isActive() && direction*control.requestedSpeedCmS()>0 &&
          direction*control.telemetry().requestedV>0,
          "a stationary endpoint just outside the band must retain a positive approach request");
  }
}

// Actuator-responsive synthetic dynamics, not a calibrated hardware model.
// Pulses arise from integrated motion; never prescribe an ideal trajectory.
struct Plant {
  Encoder encoder;
  MotorController motor;
  DistanceController control{encoder, motor};
  float x = 0, v = 0, pulseX = 0, goal = 0, load = 2.5f;
  int direction = 1;
  bool blocked = false, terminalFriction = false;
  uint32_t ticks = 0;
  Plant(uint32_t start = 1000) { fakeUs = start; }
  void start(float distance, int dir) {
    direction = dir; goal = x + distance; control.beginMove(distance, dir);
  }
  void tick() {
    fakeUs += 1000; ++ticks;
    const float u = direction * motor.currentSpeed() * 12.0f / 255;
    const float resistance = terminalFriction && goal - x < 5 ? load + 1.2f : load;
    if (blocked || (v < .01f && u < resistance + .35f)) v = 0;
    else v = max(0.0f, v + (24 * (u - resistance) - 3 * v) * .001f);
    x += v * .001f;
    while (x - pulseX >= Config::Encoder::DISTANCE_PER_PULSE_CM) {
      encoder.onPulse(); pulseX += Config::Encoder::DISTANCE_PER_PULSE_CM;
    }
    if (ticks % 50 == 0) { encoder.update(); control.update(); }
  }
  void run(float seconds = 90) {
    for (int i = 0; i < int(seconds * 1000) && control.isActive(); ++i) tick();
  }
};

void loadedEndpoints() {
  for (int dir : {-1, 1}) for (float distance : {10.f, 30.f, 100.f, 300.f}) {
    Plant p; p.terminalFriction = true; p.start(distance, dir); p.run();
    std::cout << "loaded " << dir << " " << distance << " x=" << p.x
              << " encoder=" << p.encoder.positionCm() << " state=" << p.control.statusText() << '\n';
    check(p.control.state() == DistanceController::State::COMPLETE,
          "loaded move must complete under the original GO despite terminal friction");
    const float remaining = distance - dir * p.encoder.positionCm();
    check(fabsf(remaining) <= Config::Control::POSITION_TOLERANCE_CM + .0001f,
          "completion must be inside the symmetric encoder tolerance");
    check(p.motor.currentSpeed() == 0, "terminal output must be zero");
    check(fabsf(p.x - distance) <= 1.0f,
          "synthetic wheel endpoint must remain within 1 cm including encoder quantisation");
  }
}

void continuousNominalApproach() {
  // This uncalibrated actuator-responsive model catches return of the
  // pause-and-recovery finish; it is not a physical towing validation.
  for (int direction : {-1,1}) for (float load : {1.8f,2.5f,3.3f,4.0f})
    for (float distance : {10.f,20.f,50.f,100.f}) {
      Plant p; p.load=load;p.control.setAdaptationEnabled(false);p.start(distance,direction);p.run();
      check(p.control.state()==DistanceController::State::COMPLETE && p.control.telemetry().recoveries==0,
            "nominal synthetic travel must complete continuously without recovery");
    }
}

void learningAndRecovery() {
  Plant learn; learn.start(300, 1); learn.run();
  check(learn.control.learnedStartVoltage(1) > 0, "stable travel must commit a bounded breakaway observation");
  check(learn.control.learnedStartVoltage(-1) == 0, "learning must be independent by direction");
  check(fabsf(learn.control.learnedLoadVoltage(1)) <= Config::Control::LOAD_BIAS_LIMIT_V,
        "learned running bias must be bounded");
  check(learn.control.resetLearning() && learn.control.learnedStartVoltage(1) == 0 &&
        learn.control.learnedLoadVoltage(1) == 0, "idle reset must clear RAM learning");
  Plant disabled; disabled.control.setAdaptationEnabled(false); disabled.start(100, 1); disabled.run();
  check(disabled.control.learnedStartVoltage(1) == 0 && disabled.control.learnedLoadVoltage(1) == 0,
        "disabled adaptation must not learn");
  Plant inhibit; inhibit.control.setLearningInhibited(true); inhibit.start(300, 1); inhibit.run();
  check(inhibit.control.learnedStartVoltage(1) == 0 && inhibit.control.learnedLoadVoltage(1) == 0,
        "inhibited adaptation must not learn");
  Plant recovery; recovery.start(100, 1);
  while (recovery.x < 10 && recovery.ticks < 15000 && recovery.control.isActive()) recovery.tick();
  recovery.blocked = true;
  while (recovery.control.state() != DistanceController::State::RECOVERING &&
         recovery.control.isActive() && recovery.ticks < 25000) recovery.tick();
  check(recovery.control.state() == DistanceController::State::RECOVERING, "lost progress must enter recovery");
  check(!recovery.control.resetLearning(), "active learning reset must be rejected");
  recovery.blocked = false; recovery.run();
  check(recovery.control.state() == DistanceController::State::COMPLETE,
        "bounded automatic recovery must complete without another GO");
  Plant stop; stop.start(100, 1);
  while (stop.x < 10 && stop.ticks < 15000 && stop.control.isActive()) stop.tick();
  stop.blocked = true;
  while (stop.control.state() != DistanceController::State::RECOVERING && stop.control.isActive()) stop.tick();
  stop.control.cancel(); stop.blocked = false;
  for (int i = 0; i < 12000; ++i) stop.tick();
  check(stop.motor.currentSpeed() == 0 && stop.control.state() == DistanceController::State::CANCELLED,
        "STOP during recovery must latch cancellation");
  Plant ramp; ramp.start(100, 1);
  while (ramp.x < 10 && ramp.ticks < 15000 && ramp.control.isActive()) ramp.tick();
  ramp.blocked = true;
  while (ramp.control.state() != DistanceController::State::RECOVERING && ramp.control.isActive()) ramp.tick();
  for (int i = 0; i < 1700; ++i) ramp.tick(); // 0.7 s pause then one second ramp
  check(fabsf(ramp.control.appliedVoltage()) <= .55f,
        "PWM quantization must not double the configured slow recovery ramp");
}

void changedLoadAndBlock() {
  Plant p; p.start(100, 1);
  while (p.x < 25 && p.control.isActive() && p.ticks < 15000) p.tick();
  p.load = 4; p.run();
  check(p.control.state() == DistanceController::State::COMPLETE,
        "midmove load increase must not consume a fixed trajectory deadline");
  Plant block; block.start(100, 1);
  while (block.x < 10 && block.control.isActive() && block.ticks < 15000) block.tick();
  block.blocked = true; block.run();
  check(block.control.state() == DistanceController::State::FAILED && block.motor.currentSpeed() == 0,
        "blocked running motion must terminate with bounded effort");
}

void cancellationAndCrossing() {
  for (int at : {100, 3000, 7000}) {
    Plant p; p.start(100, -1);
    for (int i = 0; i < at; ++i) p.tick();
    p.control.cancel();
    for (int i = 0; i < 5000; ++i) p.tick();
    check(p.control.state() == DistanceController::State::CANCELLED && p.motor.currentSpeed() == 0,
          "STOP must prevent every automatic recovery from restarting");
  }
  Plant over; over.start(10, 1);
  for (int i = 0; i < 17; ++i) { fakeUs += 60000; over.encoder.onPulse(); }
  over.encoder.update(); over.control.update();
  fakeUs += 2000000; over.encoder.update(); over.control.update();
  check(over.control.state() == DistanceController::State::FAILED,
        "target crossing beyond the half-centimetre band must fail rather than complete");
  Plant wrap(0xFFF00000U); wrap.start(30, 1); wrap.run();
  check(wrap.control.state() == DistanceController::State::COMPLETE,
        "microsecond wrap must preserve a loaded move");
  Plant invalid; invalid.start(std::numeric_limits<float>::quiet_NaN(), 1);
  check(!invalid.control.isActive(), "invalid distance must not actuate");
}

void slowPulsesAndSmoothOutput() {
  Plant p; p.encoder.setDistanceMode(true);
  for (int edge = 0; edge < 3; ++edge) {
    fakeUs += 800000; p.encoder.onPulse(); p.encoder.update();
  }
  for (int i = 0; i < 12; ++i) { fakeUs += 50000; p.encoder.update(); }
  check(!p.encoder.isStopped() && p.encoder.velocityCmS() > 0,
        "slow valid pulse train must not become stopped at the fixed 500 ms timeout");
  check(p.encoder.isStopped(500000), "explicit fixed stopped interval must remain available");
  Plant smooth; smooth.start(100, 1);
  float previousCommand = 0, previousVoltage = 0;
  for (int i = 0; i < 15000 && smooth.control.isActive(); ++i) {
    const auto before = smooth.control.state();
    smooth.tick();
    if (smooth.ticks % 50 != 0) continue;
    const auto after = smooth.control.state();
    const float command = fabsf(smooth.control.requestedSpeedCmS());
    const float voltage = fabsf(smooth.control.appliedVoltage());
    if (before == DistanceController::State::MOVING && after == before) {
      check(command - previousCommand <= .501f && previousCommand - command <= .251f,
            "normal velocity request must respect acceleration/deceleration limits");
      check(fabsf(voltage - previousVoltage) <= 1.30f,
            "normal motor effort must respect configured voltage slew plus PWM quantization");
    }
    check(smooth.motor.currentSpeed() >= 0, "motion controller must never command reversal");
    previousCommand = command; previousVoltage = voltage;
  }
}

void rampStoppingProtection() {
  for (auto phase : {DistanceController::State::STARTING, DistanceController::State::RECOVERING}) {
    Plant p; p.start(3, 1);
    p.control.state_ = phase;
    if (phase == DistanceController::State::RECOVERING)
      p.control.phaseUs_ = fakeUs - Config::Control::RECOVERY_PAUSE_US;
    // Real encoder feedback: two forward pulses 40 ms apart, before the band.
    for (int i = 0; i < 2; ++i) { fakeUs += 40000; p.encoder.onPulse(); }
    p.encoder.update();
    p.control.update();
    check(p.motor.currentSpeed() == 0 && p.control.state() == DistanceController::State::FAILED &&
          std::string(p.control.failureText()) == "BRAKING_LIMIT",
          "startup/recovery must stop and report an infeasible measured braking envelope");
  }
}

void independentLearningAndDirectionalLoads() {
  Plant startsOnly; startsOnly.control.setAdaptationEnabled(true, false);
  startsOnly.start(300, 1); startsOnly.run();
  check(startsOnly.control.learnedStartVoltage(1) > 0 && startsOnly.control.learnedLoadVoltage(1) == 0,
        "breakaway learning must operate with running-load learning disabled");
  Plant loadOnly; loadOnly.control.setAdaptationEnabled(false, true);
  loadOnly.start(300, 1); loadOnly.run();
  check(loadOnly.control.learnedStartVoltage(1) == 0 && fabsf(loadOnly.control.learnedLoadVoltage(1)) > .01f,
        "running-load learning must operate with breakaway learning disabled");
  Plant both;
  both.load = 2; both.start(300, 1); both.run();
  const float forwardStart = both.control.learnedStartVoltage(1);
  const float forwardLoad = both.control.learnedLoadVoltage(1);
  both.load = 4; both.start(300, -1); both.run();
  check(both.control.state() == DistanceController::State::COMPLETE &&
        both.control.learnedStartVoltage(-1) > forwardStart &&
        both.control.learnedStartVoltage(1) == forwardStart &&
        both.control.learnedLoadVoltage(1) == forwardLoad,
        "different directional loads must keep separate learned values");
  Plant busy; busy.start(100, 1); busy.control.setAdaptationEnabled(false, false);
  check(busy.control.startAdaptationEnabled() && busy.control.loadAdaptationEnabled(),
        "independent learning switches must not change during an authorized attempt");
}

void explicitBoundsAndPhaseStops() {
  Plant timeout; timeout.start(100, 1);
  while (timeout.x < 10 && timeout.control.isActive() && timeout.ticks < 15000) timeout.tick();
  timeout.blocked = true;
  timeout.control.deadlineUs_ = fakeUs - timeout.control.startUs_ + 3000000;
  const uint32_t deadline = timeout.control.deadlineUs_;
  timeout.run();
  check(std::string(timeout.control.failureText()) == "MOVE_TIMEOUT" &&
        timeout.control.telemetry().recoveries > 0 && timeout.motor.currentSpeed() == 0 &&
        fakeUs - timeout.control.startUs_ <= deadline + 50000,
        "recovery must not extend the absolute authorization deadline");
  Plant blocked; blocked.blocked = true; blocked.start(100, 1); blocked.run();
  check(std::string(blocked.control.failureText()) == "START_TIMEOUT" && blocked.motor.currentSpeed() == 0,
        "a blocked startup must fail at its own effort/time bound");
  Plant assisted; assisted.load = -1; assisted.start(100, 1);
  bool reversed = false;
  for (int i = 0; i < 90000 && assisted.control.isActive(); ++i) {
    assisted.tick(); reversed |= assisted.motor.currentSpeed() < 0;
  }
  check(!assisted.control.isActive() && assisted.motor.currentSpeed() == 0 && !reversed &&
        assisted.control.state() == DistanceController::State::FAILED,
        "persistent assisted motion must terminate without pretending zero PWM can stop it or reversing");
  for (auto phase : {DistanceController::State::STARTING, DistanceController::State::MOVING,
                     DistanceController::State::APPROACHING, DistanceController::State::SETTLING,
                     DistanceController::State::RECOVERING}) {
    Plant stop; stop.start(100, 1);
    for (int i = 0; i < 90000 && stop.control.isActive() && stop.control.state() != phase; ++i) {
      if (phase == DistanceController::State::RECOVERING && stop.x > 10) stop.blocked = true;
      stop.tick();
    }
    check(stop.control.state() == phase, "STOP regression must reach its named phase");
    stop.control.cancel(); stop.blocked = false;
    for (int i = 0; i < 3000; ++i) stop.tick();
    check(stop.motor.currentSpeed() == 0 && stop.control.state() == DistanceController::State::CANCELLED,
          "STOP in every active phase must prevent an automatic restart");
  }
}

int main() {
  symmetricEndpointBand();
  terminalRoomOutsideBand();
  loadedEndpoints(); continuousNominalApproach(); changedLoadAndBlock(); cancellationAndCrossing(); learningAndRecovery(); slowPulsesAndSmoothOutput();
  rampStoppingProtection();
  independentLearningAndDirectionalLoads(); explicitBoundsAndPhaseStops();
  std::cout << "Loaded controller checks: " << failures << " failures\n";
  return failures ? 1 : 0;
}
