#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
using std::min;
using std::max;
constexpr float PI = 3.14159265358979323846f;
#define IRAM_ATTR
#define OUTPUT 1
#define INPUT_PULLUP 2
#define RISING 3
#define HIGH 1
#define LOW 0
inline uint32_t fakeUs = 0;
inline uint32_t micros() { return fakeUs; }
inline uint32_t millis() { return fakeUs / 1000; }
inline void delay(uint32_t ms) { fakeUs += ms * 1000; }
inline void noInterrupts() {}
inline void interrupts() {}
inline void pinMode(uint8_t, int) {}
inline void digitalWrite(uint8_t, int) {}
inline int digitalPinToInterrupt(uint8_t pin) { return pin; }
inline void attachInterrupt(int, void (*)(), int) {}
inline void ledcSetup(uint8_t, uint32_t, uint8_t) {}
inline void ledcAttachPin(uint8_t, uint8_t) {}
inline void ledcWrite(uint8_t, int) {}
template <typename T> T constrain(T x, T lo, T hi) {
  return min(max(x, lo), hi);
}
struct FakeSerial {
  void begin(int) {}
  template <typename T> void print(T, int = 0) {}
  template <typename T> void println(T, int = 0) {}
};
inline FakeSerial Serial;
