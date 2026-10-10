#pragma once
#include <cstdint>
#include <cmath>
constexpr float PI = 3.14159265358979323846f;
constexpr int INPUT = 0, ADC_11db = 3;
extern uint32_t clockMs, adcMv, reads;
inline uint32_t millis() { return clockMs; }
inline void pinMode(int, int) {}
inline void analogReadResolution(int) {}
inline void analogSetPinAttenuation(int, int) {}
inline uint32_t analogReadMilliVolts(int) { ++reads; return adcMv; }
