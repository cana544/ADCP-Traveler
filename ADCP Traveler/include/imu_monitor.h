#pragma once
#include <Arduino.h>
#include <sh2.h>
#include <sh2_SensorValue.h>
#include "bno_uart_transport.h"
#include "bno_shtp_guard.h"
#include "imu_data.h"

// SH-2 is single-instance; this application owns exactly one diagnostic sensor.
class ImuMonitor {
 public:
  void begin();
  void update();
  const ImuData& snapshot() const { return data_; }
  void startQualityCheck() { data_.startQualityCheck(); }
  uint32_t transportErrors() const { return rx_.errors() + guard_.errors(); }
 private:
  static ImuMonitor* instance_;
  sh2_Hal_t hal_ = {};
  ImuData data_;
  BnoUartTransport rx_;
  BnoShtpGuard guard_;
  bool opened_ = false;
  uint8_t configIndex_ = 0;
  uint32_t lastConfigUs_ = 0;
  static int open(sh2_Hal_t*);
  static void close(sh2_Hal_t*);
  static int read(sh2_Hal_t*, uint8_t*, unsigned, uint32_t*);
  static int write(sh2_Hal_t*, uint8_t*, unsigned);
  static uint32_t timeUs(sh2_Hal_t*);
  static void event(void*, sh2_AsyncEvent_t*);
  static void sensor(void*, sh2_SensorEvent_t*);
};
