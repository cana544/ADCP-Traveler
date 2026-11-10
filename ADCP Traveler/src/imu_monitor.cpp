#include "imu_monitor.h"
#include <sh2_err.h>

ImuMonitor* ImuMonitor::instance_ = nullptr;

int ImuMonitor::open(sh2_Hal_t*) {
  Serial2.setRxBufferSize(1024);
  // BNO085 UART-SHTP uses 3 Mbaud (also selected inside Adafruit begin_UART).
  Serial2.begin(3000000, SERIAL_8N1, 16, 17);
  uint8_t reset[] = {5, 0, 1, 0, 1};
  return write(nullptr, reset, sizeof(reset)) == sizeof(reset) ? 0 : -1;
}
void ImuMonitor::close(sh2_Hal_t*) { Serial2.end(); }
uint32_t ImuMonitor::timeUs(sh2_Hal_t*) { return micros(); }

int ImuMonitor::read(sh2_Hal_t*, uint8_t* output, unsigned capacity, uint32_t* timestamp) {
  // Return immediately on partial/missing packets; retain fragments for next call.
  for (unsigned budget = 0; budget < 96 && Serial2.available() > 0; ++budget) {
    const size_t n = instance_->rx_.feed(static_cast<uint8_t>(Serial2.read()), output, capacity);
    if (n && instance_->guard_.accept(output, n)) {
      *timestamp = micros(); return static_cast<int>(n);
    }
  }
  return 0;
}

int ImuMonitor::write(sh2_Hal_t*, uint8_t* payload, unsigned length) {
  if (length > SH2_HAL_MAX_TRANSFER_OUT) return -1;
  uint8_t frame[2 * SH2_HAL_MAX_TRANSFER_OUT + 3];
  const size_t n = BnoUartTransport::encode(payload, length, frame, sizeof(frame));
  // SHTP retries zero indefinitely. A negative result aborts this transaction;
  // report configuration can retry on a later loop without blocking control.
  if (!n || Serial2.availableForWrite() < static_cast<int>(n)) return -1;
  return Serial2.write(frame, n) == n ? static_cast<int>(length) : -1;
}

void ImuMonitor::event(void* cookie, sh2_AsyncEvent_t* event) {
  auto& monitor = *static_cast<ImuMonitor*>(cookie);
  if (event->eventId == SH2_RESET) {
    monitor.data_.reset(); monitor.configIndex_ = 0;
  } else if (event->eventId == SH2_SHTP_EVENT) ++monitor.data_.errors;
}

void ImuMonitor::sensor(void* cookie, sh2_SensorEvent_t* event) {
  auto& data = static_cast<ImuMonitor*>(cookie)->data_;
  sh2_SensorValue_t value = {};
  if (sh2_decodeSensorEvent(&value, event) != SH2_OK) { ++data.errors; return; }
  const uint32_t now = micros();
  switch (value.sensorId) {
    case SH2_ACCELEROMETER:
      data.vector(ImuData::ACCEL, value.un.accelerometer.x, value.un.accelerometer.y,
                  value.un.accelerometer.z, now, value.timestamp, value.status); break;
    case SH2_LINEAR_ACCELERATION:
      data.vector(ImuData::LINEAR, value.un.linearAcceleration.x, value.un.linearAcceleration.y,
                  value.un.linearAcceleration.z, now, value.timestamp, value.status); break;
    case SH2_GYROSCOPE_CALIBRATED:
      data.vector(ImuData::GYRO, value.un.gyroscope.x, value.un.gyroscope.y,
                  value.un.gyroscope.z, now, value.timestamp, value.status); break;
    case SH2_GAME_ROTATION_VECTOR:
      // No magnetometer heading: avoids relying on yaw near the drive motor.
      data.quaternion(value.un.gameRotationVector.i, value.un.gameRotationVector.j,
          value.un.gameRotationVector.k, value.un.gameRotationVector.real,
          now, value.timestamp, value.status); break;
    default: break;
  }
}

void ImuMonitor::begin() {
  instance_ = this;
  hal_.open = open; hal_.close = close; hal_.read = read;
  hal_.write = write; hal_.getTimeUs = timeUs;
  // The core's advertisement wait is bounded at 200 ms, before UI/control startup.
  opened_ = sh2_open(&hal_, event, this) == SH2_OK;
  if (opened_) sh2_setSensorCallback(sensor, this);
  else ++data_.errors;
}

void ImuMonitor::update() {
  if (!opened_) return;
  sh2_service(); // one bounded HAL read; never calls the stock UART HAL
  if (configIndex_ < 4 && micros() - lastConfigUs_ >= 100000UL) {
    lastConfigUs_ = micros();
    const sh2_SensorId_t ids[] = {SH2_ACCELEROMETER, SH2_LINEAR_ACCELERATION,
                                 SH2_GYROSCOPE_CALIBRATED, SH2_GAME_ROTATION_VECTOR};
    sh2_SensorConfig_t config = {};
    config.reportInterval_us = 20000;
    // In pinned SH-2 this operation completes synchronously after sendCtrl,
    // without waiting for a sensor response. HAL write never waits for TX space.
    if (sh2_setSensorConfig(ids[configIndex_], &config) == SH2_OK) ++configIndex_;
    else ++data_.errors;
  }
}
