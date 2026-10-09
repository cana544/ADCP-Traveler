#ifndef ENCODER_H
#define ENCODER_H

#include <Arduino.h>

class Encoder {
 public:
  Encoder();

  void begin(uint8_t pin);
  void update();
  void setDirection(int direction);
  void setDistanceMode(bool enabled);
  void zero();

  float positionCm() const;
  float velocityCmS() const;
  bool isStopped(uint32_t timeoutUs = 0) const;
  int32_t pulseCount() const;

 private:
  static Encoder* activeInstance_;
  static void IRAM_ATTR handleInterrupt();
  void IRAM_ATTR onPulse();

  uint8_t pin_;
  volatile int32_t signedPulses_;
  volatile int8_t direction_;
  volatile uint32_t lastPulseUs_;
  volatile uint32_t latestPeriodUs_;
  volatile bool distanceMode_;

  int32_t zeroOffsetPulses_;
  int32_t lastProcessedPulses_;
  float filteredVelocityCmS_;
  float velocitySamples_[4];
  uint8_t velocitySampleIndex_;
  uint8_t velocitySampleCount_;
  void updateDistanceVelocity(int32_t pulses, uint32_t lastPulseUs,
                              uint32_t periodUs, uint32_t nowUs);
};

#endif
