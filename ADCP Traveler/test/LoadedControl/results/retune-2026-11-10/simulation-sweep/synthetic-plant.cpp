#include "Arduino.h"
#include <iostream>
#include <limits>
#define private public
#include "distance_controller.h"
#undef private
#include "src/motion_profile.cpp"
#include "src/encoder.cpp"
#include "src/motor_controller.cpp"
#include "src/distance_controller.cpp"

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


int main(){ for(float load:{1.8f,2.5f,3.3f,4.0f}) for(float d:{10.f,20.f,50.f,100.f}) { Plant p;p.load=load;p.control.setAdaptationEnabled(false);p.start(d,1);p.run(); std::cout<<load<<","<<d<<","<<p.control.statusText()<<","<<int(p.control.telemetry().recoveries)<<","<<p.control.remainingCm()<<","<<p.ticks*.001f<<"\n";} }
