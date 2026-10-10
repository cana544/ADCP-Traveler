#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
using std::min;
using std::max;
using std::isfinite;
constexpr float PI = 3.14159265358979323846f;
#define IRAM_ATTR
#define INPUT_PULLUP 2
#define OUTPUT 1
#define HIGH 1
#define LOW 0
#define RISING 1
#define ESP_ARDUINO_VERSION_MAJOR 2
extern uint32_t testTimeUs;
extern void (*testPulse)();
inline uint32_t micros() { return testTimeUs; }
inline void noInterrupts() {}
inline void interrupts() {}
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int, void (*callback)(), int) { testPulse = callback; }
inline void ledcSetup(int, int, int) {}
inline void ledcAttachPin(int, int) {}
inline void ledcWrite(int, int) {}
template <typename T> T constrain(T value, T low, T high) { return min(high, max(low, value)); }
struct SerialStub { template <typename... T> void printf(const char*, T...) {} };
extern SerialStub Serial;
