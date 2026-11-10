#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include "bno_uart_transport.h"
#include "imu_data.h"

int main() {
  BnoUartTransport rx;
  const uint8_t payload[] = {6, 0, 3, 0, 0x7E, 0x7D};
  uint8_t framed[32] = {}, decoded[384] = {};
  const size_t size = BnoUartTransport::encode(payload, sizeof(payload), framed, sizeof(framed));
  assert(size == 11); // two delimiters, protocol byte, six bytes, two escapes
  for (size_t i = 0; i + 1 < size; ++i) assert(rx.feed(framed[i], decoded, sizeof(decoded)) == 0);
  assert(rx.feed(framed[size-1], decoded, sizeof(decoded)) == sizeof(payload));
  for (size_t i = 0; i < sizeof(payload); ++i) assert(decoded[i] == payload[i]);
  // Noise, incomplete escape, oversized packet and invalid length resynchronize.
  rx.feed(0x7E, decoded, sizeof(decoded)); rx.feed(1, decoded, sizeof(decoded));
  for (int i = 0; i < 600; ++i) rx.feed(42, decoded, sizeof(decoded));
  rx.feed(0x7E, decoded, sizeof(decoded));
  size_t count = 0;
  for (uint8_t b : framed) count += rx.feed(b, decoded, sizeof(decoded));
  assert(count == sizeof(payload) && rx.errors() > 0);
  const uint8_t bad[] = {0x7E, 1, 9, 0, 3, 0, 0x7E};
  for (uint8_t b : bad) assert(rx.feed(b, decoded, sizeof(decoded)) == 0);
  assert(BnoUartTransport::encode(payload, sizeof(payload), framed, 3) == 0);

  ImuData data;
  assert(!data.healthy(1000));
  data.vector(ImuData::ACCEL, 0, 0, 9.80665f, 1000, 900, 3);
  data.vector(ImuData::LINEAR, 0, 0, 0, 1000, 900, 3);
  data.vector(ImuData::GYRO, 0, 0, 0, 1000, 900, 3);
  data.quaternion(0, 0, 0, 1, 1000, 900, 3);
  assert(data.healthy(2000));
  // One live report cannot hide staleness of another required report.
  data.vector(ImuData::LINEAR, 0, 0, 0, 700000, 699900, 3);
  assert(!data.healthy(700000));
  data.reset(); assert(!data.healthy(700000) && data.resets == 1);
  data.vector(ImuData::ACCEL, NAN, 0, 0, 1, 1, 0);
  assert(!data.valid[ImuData::ACCEL]);
  // Quality check describes a stationary calibration window, never body velocity.
  data.startQualityCheck();
  for (uint32_t i = 1; i <= 150; ++i) {
    const uint32_t t = i * 20000;
    data.vector(ImuData::GYRO, 0, 0, 0, t, t, 3);
    data.vector(ImuData::ACCEL, 0, 0, 9.80665f, t, t, 3);
  }
  assert(data.qualityReady && data.qualityPass);
  data.startQualityCheck();
  for (uint32_t i = 1; i <= 150; ++i) {
    const uint32_t t = i * 20000;
    data.vector(ImuData::GYRO, 1, 0, 0, t, t, 3);
    data.vector(ImuData::ACCEL, 0, 0, 9.80665f, t, t, 3);
  }
  assert(data.qualityReady && !data.qualityPass);
  std::cout << "IMU transport, independent freshness, invalid data and quality checks passed\n";
}
