#include "Arduino.h"
#include <cassert>
#include <iostream>
#include <sh2.h>
#include <sh2_SensorValue.h>
#include "bno_uart_transport.h"
#include "imu_data.h"
#define private public
#include "imu_monitor.h"
#undef private
int main() {
  ImuMonitor monitor;
  const uint32_t start = fakeUs;
  monitor.begin();
  assert(fakeUs - start < 202000); // real SH-2's boot advertisement timeout
  assert(!monitor.snapshot().healthy(fakeUs));
  for (int i = 0; i < 1000; ++i) Serial2.rx.push_back(42);
  const size_t count = Serial2.rx.size();
  monitor.update();
  assert(count - Serial2.rx.size() <= 96);
  Serial2.room = 0;
  uint8_t payload[] = {5, 0, 1, 0, 1};
  assert(ImuMonitor::write(nullptr, payload, sizeof(payload)) < 0);
  assert(ImuMonitor::write(nullptr, payload, 1000) < 0);
  // Directly call the real config API under backpressure. Zero HAL return
  // would make SHTP retry indefinitely; negative return aborts this call.
  for (int i = 0; i < 20; ++i) { fakeUs += 100000; monitor.update(); }
  assert(!monitor.snapshot().healthy(fakeUs));
  sh2_AsyncEvent_t reset = {}; reset.eventId = SH2_RESET;
  ImuMonitor::event(&monitor, &reset);
  assert(monitor.snapshot().resets == 1 && monitor.configIndex_ == 0);
  Serial2.rx.clear(); Serial2.room = 128;
  auto enqueue = [](const std::vector<uint8_t>& packet) {
    uint8_t frame[800];
    const size_t n = BnoUartTransport::encode(packet.data(), packet.size(), frame, sizeof(frame));
    for (size_t i = 0; i < n; ++i) Serial2.rx.push_back(frame[i]);
  };
  std::vector<uint8_t> longName = {48, 0, 0, 0, 0, 8, 41};
  for (int i = 0; i < 40; ++i) longName.push_back('A');
  longName.push_back(0);
  enqueue(longName);
  uint8_t output[384] = {}; uint32_t stamp = 0;
  // Reject before dispatch: the pinned core would strcpy into appName[32].
  assert(ImuMonitor::read(nullptr, output, sizeof(output), &stamp) == 0);
  enqueue(longName); monitor.update();
  assert(Serial2.rx.empty() && monitor.transportErrors() > 0);
  auto packet = [](uint8_t channel, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> result = {uint8_t(payload.size() + 4), uint8_t((payload.size() + 4) >> 8), channel, 0};
    result.insert(result.end(), payload.begin(), payload.end()); return result;
  };
  std::vector<uint8_t> advert = {0};
  auto tlv = [&](uint8_t tag, const std::vector<uint8_t>& value) {
    advert.push_back(tag); advert.push_back(uint8_t(value.size()));
    advert.insert(advert.end(), value.begin(), value.end());
  };
  auto name = [&](uint8_t tag, const char* text) { tlv(tag, std::vector<uint8_t>(text, text + strlen(text) + 1)); };
  tlv(1, {1,0,0,0}); name(8, "executable"); tlv(6, {1}); name(9, "device");
  tlv(1, {2,0,0,0}); name(8, "sensorhub"); tlv(6, {2}); name(9, "control");
  tlv(6, {3}); name(9, "inputNormal"); tlv(6, {5}); name(9, "inputGyroRv");
  tlv(0x81, {1,10,2,10,4,10,8,12,0xFB,5});
  enqueue(packet(0, advert));
  for (int i = 0; i < 10 && !Serial2.rx.empty(); ++i) monitor.update();
  assert(Serial2.rx.empty());
  // Missing gyro-integrated length must be rejected before the real core's
  // reportLen==0 loop, although the channel itself has been advertised.
  const uint32_t errors = monitor.transportErrors();
  enqueue(packet(5, {1})); monitor.update();
  assert(monitor.transportErrors() == errors + 1);
  // Valid advertised accelerometer report passes through the real decoder.
  enqueue(packet(3, {1,0,3,0,0,0,0,0,0xCE,9})); monitor.update();
  assert(monitor.snapshot().valid[ImuData::ACCEL]);
  assert(monitor.snapshot().values[ImuData::ACCEL][2] > 9.7f);
  // Payload shorter than its advertised report, or a truncated TLV, is rejected.
  const uint32_t beforeBad = monitor.transportErrors();
  enqueue(packet(3, {1,0,3})); monitor.update();
  enqueue(packet(0, {0,1,4,2})); monitor.update();
  assert(monitor.transportErrors() == beforeBad + 2);
  std::cout << "Real SH-2 absent sensor, bounded UART/TX, reset, malformed complete packets and valid decoding passed\n";
}
