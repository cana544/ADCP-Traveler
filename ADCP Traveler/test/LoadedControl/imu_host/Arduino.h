#pragma once
#define micros baseMicros
#include "../../DistanceControlTest/host/Arduino.h"
#undef micros
#include <deque>
#include <vector>
inline uint32_t micros() { fakeUs += 10; return fakeUs; }
#define SERIAL_8N1 0
struct FakeUart {
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
  int room = 128;
  void setRxBufferSize(int) {}
  void begin(int, int, int, int) {}
  void end() {}
  int available() { return int(rx.size()); }
  int availableForWrite() { return room; }
  int read() { if (rx.empty()) return -1; int c = rx.front(); rx.pop_front(); return c; }
  size_t write(const uint8_t* data, size_t size) { tx.insert(tx.end(), data, data + size); return size; }
};
inline FakeUart Serial2;
