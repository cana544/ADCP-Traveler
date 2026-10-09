#include "encoder.h"

#include "config.h"

Encoder* Encoder::activeInstance_ = nullptr;

Encoder::Encoder()
    : pin_(Config::Pins::ENCODER),
      signedPulses_(0),
      direction_(1),
      lastPulseUs_(0),
      latestPeriodUs_(0),
      distanceMode_(false),
      zeroOffsetPulses_(0),
      lastProcessedPulses_(0),
      filteredVelocityCmS_(0.0f),
      velocitySampleIndex_(0),
      velocitySampleCount_(0) {
  for (float& sample : velocitySamples_) {
    sample = 0.0f;
  }
}

void Encoder::begin(uint8_t pin) {
  pin_ = pin;
  activeInstance_ = this;
  pinMode(pin_, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pin_), handleInterrupt, RISING);
}

void IRAM_ATTR Encoder::handleInterrupt() {
  if (activeInstance_ != nullptr) {
    activeInstance_->onPulse();
  }
}

void IRAM_ATTR Encoder::onPulse() {
  const uint32_t nowUs = micros();

  if (distanceMode_ && lastPulseUs_ != 0 &&
      nowUs - lastPulseUs_ < Config::Encoder::DISTANCE_MIN_PULSE_INTERVAL_US) {
    return;
  }

  if (lastPulseUs_ != 0) {
    latestPeriodUs_ = nowUs - lastPulseUs_;
  }

  lastPulseUs_ = nowUs;
  signedPulses_ += direction_;
}

void Encoder::setDirection(int direction) {
  if (direction > 0) {
    direction_ = 1;
  } else if (direction < 0) {
    direction_ = -1;
  }
}

void Encoder::setDistanceMode(bool enabled) {
  if (distanceMode_ == enabled) return;
  noInterrupts();
  distanceMode_ = enabled;
  latestPeriodUs_ = 0;
  // A new distance move starts only after the wheel has stopped. Its first
  // velocity interval must not include idle time since the previous move.
  if (enabled) lastPulseUs_ = 0;
  lastProcessedPulses_ = signedPulses_;
  interrupts();
  filteredVelocityCmS_ = 0.0f;
  velocitySampleIndex_ = velocitySampleCount_ = 0;
  for (float& sample : velocitySamples_) sample = 0.0f;
}

void Encoder::update() {
  int32_t pulses;
  uint32_t lastPulseUs;
  uint32_t periodUs;

  noInterrupts();
  pulses = signedPulses_;
  lastPulseUs = lastPulseUs_;
  periodUs = latestPeriodUs_;
  interrupts();

  const uint32_t nowUs = micros();
  const bool stopped =
      lastPulseUs == 0 || (nowUs - lastPulseUs) >=
          (distanceMode_ ? Config::Encoder::DISTANCE_STOP_TIMEOUT_US
                         : Config::Encoder::STOP_TIMEOUT_US);

  if (stopped) {
    filteredVelocityCmS_ = 0.0f;
    velocitySampleIndex_ = 0;
    velocitySampleCount_ = 0;
    for (float& sample : velocitySamples_) {
      sample = 0.0f;
    }
    lastProcessedPulses_ = pulses;
    return;
  }

  if (distanceMode_) {
    updateDistanceVelocity(pulses, lastPulseUs, periodUs, nowUs);
    return;
  }

  if (pulses == lastProcessedPulses_ || periodUs == 0) {
    return;
  }

  const int pulseDirection = pulses > lastProcessedPulses_ ? 1 : -1;
  const float rawVelocityCmS =
      pulseDirection * Config::Encoder::DISTANCE_PER_PULSE_CM * 1000000.0f /
      static_cast<float>(periodUs);

  velocitySamples_[velocitySampleIndex_] = rawVelocityCmS;
  velocitySampleIndex_ =
      (velocitySampleIndex_ + 1) % Config::Encoder::VELOCITY_FILTER_SAMPLES;
  if (velocitySampleCount_ < Config::Encoder::VELOCITY_FILTER_SAMPLES) {
    velocitySampleCount_++;
  }

  float sum = 0.0f;
  for (uint8_t i = 0; i < velocitySampleCount_; ++i) {
    sum += velocitySamples_[i];
  }
  filteredVelocityCmS_ = sum / static_cast<float>(velocitySampleCount_);
  lastProcessedPulses_ = pulses;
}

void Encoder::updateDistanceVelocity(int32_t pulses, uint32_t lastPulseUs,
                                     uint32_t periodUs, uint32_t nowUs) {
  if (periodUs == 0) return;
  if (pulses != lastProcessedPulses_) {
    const int pulseDirection = pulses > lastProcessedPulses_ ? 1 : -1;
    velocitySamples_[velocitySampleIndex_] =
        pulseDirection * static_cast<float>(periodUs);
    velocitySampleIndex_ = (velocitySampleIndex_ + 1) %
        Config::Encoder::DISTANCE_FILTER_SAMPLES;
    if (velocitySampleCount_ < Config::Encoder::DISTANCE_FILTER_SAMPLES)
      velocitySampleCount_++;
    lastProcessedPulses_ = pulses;
  }
  if (velocitySampleCount_ == 0) return;
  float sum = 0;
  for (uint8_t i = 0; i < velocitySampleCount_; ++i) sum += velocitySamples_[i];
  float period = sum / static_cast<float>(velocitySampleCount_);
  if (velocitySampleCount_ == 3) {
    const float a = velocitySamples_[0], b = velocitySamples_[1], c = velocitySamples_[2];
    period = max(min(a, b), min(max(a, b), c));
  }
  filteredVelocityCmS_ = Config::Encoder::DISTANCE_PER_PULSE_CM * 1000000.0f / period;
  const uint32_t age = nowUs - lastPulseUs;
  if (age > fabsf(period)) {
    const float speed = Config::Encoder::DISTANCE_PER_PULSE_CM * 1000000.0f / age;
    filteredVelocityCmS_ = copysignf(min(fabsf(filteredVelocityCmS_), speed),
                                     filteredVelocityCmS_);
  }
}

void Encoder::zero() {
  noInterrupts();
  zeroOffsetPulses_ = signedPulses_;
  interrupts();
}

float Encoder::positionCm() const {
  int32_t pulses;
  noInterrupts();
  pulses = signedPulses_;
  interrupts();
  return static_cast<float>(pulses - zeroOffsetPulses_) *
         Config::Encoder::DISTANCE_PER_PULSE_CM;
}

float Encoder::velocityCmS() const { return filteredVelocityCmS_; }

bool Encoder::isStopped(uint32_t timeoutUs) const {
  uint32_t lastPulseUs;
  noInterrupts();
  lastPulseUs = lastPulseUs_;
  interrupts();
  if (timeoutUs == 0) {
    timeoutUs = distanceMode_ ? Config::Encoder::DISTANCE_STOP_TIMEOUT_US
                             : Config::Encoder::STOP_TIMEOUT_US;
  }
  return lastPulseUs == 0 || (micros() - lastPulseUs) >= timeoutUs;
}

int32_t Encoder::pulseCount() const {
  int32_t pulses;
  noInterrupts();
  pulses = signedPulses_;
  interrupts();
  return pulses;
}
