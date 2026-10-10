#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H
#include <Arduino.h>

class BatteryMonitor {
 public:
  void begin();
  void update();  // At most one calibrated ADC conversion per call; no delays.
  bool valid() const { return valid_; }
  float voltage() const { return voltage_; }
  float percent() const { return estimatePercent(voltage_); }
  static float voltageFromMillivolts(float millivolts);
  static float estimatePercent(float voltage);
 private:
  bool valid_ = false;
  bool sampling_ = false;
  bool invalidSample_ = false;
  uint8_t samples_ = 0;
  uint32_t sumMv_ = 0;
  uint32_t lastCycleMs_ = 0;
  uint32_t lastSampleMs_ = 0;
  float voltage_ = 0;
};
#endif
