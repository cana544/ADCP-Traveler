#include <Arduino.h>

// Command-driven standalone tuning sketch. Boot remains idle.
// Runtime defaults and commands are in tuning_control.h.

namespace Config {
namespace Pins {
constexpr uint8_t MOTOR_RPWM = 25;
constexpr uint8_t MOTOR_LPWM = 26;
constexpr uint8_t MOTOR_REN = 27;
constexpr uint8_t MOTOR_LEN = 14;
constexpr uint8_t ENCODER = 21;
}  // namespace Pins

namespace EncoderConfig {
constexpr float SLOTS_PER_REV = 50.0f;
constexpr float WHEEL_RADIUS_CM = 5.0f;
constexpr float DISTANCE_PER_PULSE_CM =
    (2.0f * PI * WHEEL_RADIUS_CM) / SLOTS_PER_REV;
// One pulse is 0.628 cm: a 100 ms timeout falsely reports a stop
// whenever speed falls below 6.28 cm/s during deceleration.
constexpr uint32_t STOP_TIMEOUT_US = 500000UL;
// 15 ms permits 41.9 cm/s, above the allowed 30 cm/s command limit,
// while rejecting the 7–8 ms extra edges seen in the baseline captures.
constexpr uint32_t MIN_PULSE_INTERVAL_US = 15000UL;
constexpr uint8_t VELOCITY_FILTER_SAMPLES = 3;
}  // namespace EncoderConfig

namespace Control {
constexpr float SUPPLY_VOLTAGE = 12.0f;
constexpr uint32_t CONTROL_PERIOD_US = 50000UL;
}  // namespace Control
}  // namespace Config

struct MotionReference {
  float positionCm;
  float velocityCmS;
  bool finished;
};

class MotorController {
 public:
  MotorController(uint8_t rpwmChannel = 0, uint8_t lpwmChannel = 1,
                  uint32_t frequency = 20000, uint8_t resolutionBits = 8)
      : rpwmPin_(Config::Pins::MOTOR_RPWM),
        lpwmPin_(Config::Pins::MOTOR_LPWM),
        renPin_(Config::Pins::MOTOR_REN),
        lenPin_(Config::Pins::MOTOR_LEN),
        rpwmChannel_(rpwmChannel),
        lpwmChannel_(lpwmChannel),
        frequency_(frequency),
        resolutionBits_(resolutionBits),
        enabled_(false),
        currentSpeed_(0) {}

  void begin(uint8_t rpwmPin, uint8_t lpwmPin, uint8_t renPin,
             uint8_t lenPin) {
    rpwmPin_ = rpwmPin;
    lpwmPin_ = lpwmPin;
    renPin_ = renPin;
    lenPin_ = lenPin;

    pinMode(renPin_, OUTPUT);
    pinMode(lenPin_, OUTPUT);
    attachPwmPin(rpwmPin_, rpwmChannel_);
    attachPwmPin(lpwmPin_, lpwmChannel_);

    stop();
    setEnabled(true);
  }

  void setEnabled(bool enabled) {
    enabled_ = enabled;
    digitalWrite(renPin_, enabled_ ? HIGH : LOW);
    digitalWrite(lenPin_, enabled_ ? HIGH : LOW);

    if (!enabled_) {
      stop();
    } else {
      applySpeed();
    }
  }

  void setSpeed(int speed) {
    currentSpeed_ = constrain(speed, -MAX_DUTY, MAX_DUTY);
    if (!enabled_) {
      setEnabled(true);
    } else {
      applySpeed();
    }
  }

  void setVoltage(float voltage) {
    const float limited = constrain(voltage, -Config::Control::SUPPLY_VOLTAGE,
                                    Config::Control::SUPPLY_VOLTAGE);
    const float dutyScale =
        static_cast<float>(MAX_DUTY) / Config::Control::SUPPLY_VOLTAGE;
    setSpeed(static_cast<int>(roundf(limited * dutyScale)));
  }

  void stop() {
    currentSpeed_ = 0;
    applySpeed();
  }

  bool isEnabled() const { return enabled_; }
  int currentSpeed() const { return currentSpeed_; }

 private:
  static constexpr int MAX_DUTY = 255;

  uint8_t rpwmPin_;
  uint8_t lpwmPin_;
  uint8_t renPin_;
  uint8_t lenPin_;
  uint8_t rpwmChannel_;
  uint8_t lpwmChannel_;
  uint32_t frequency_;
  uint8_t resolutionBits_;
  bool enabled_;
  int currentSpeed_;

  void attachPwmPin(uint8_t pin, uint8_t channel) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttachChannel(pin, frequency_, resolutionBits_, channel);
#else
    ledcSetup(channel, frequency_, resolutionBits_);
    ledcAttachPin(pin, channel);
#endif
  }

  void writePwmDuty(uint8_t pin, uint8_t channel, int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    (void)channel;
    ledcWrite(pin, duty);
#else
    (void)pin;
    ledcWrite(channel, duty);
#endif
  }

  void applySpeed() {
    if (!enabled_) {
      writePwmDuty(rpwmPin_, rpwmChannel_, 0);
      writePwmDuty(lpwmPin_, lpwmChannel_, 0);
      return;
    }

    if (currentSpeed_ > 0) {
      writePwmDuty(rpwmPin_, rpwmChannel_, currentSpeed_);
      writePwmDuty(lpwmPin_, lpwmChannel_, 0);
    } else if (currentSpeed_ < 0) {
      writePwmDuty(rpwmPin_, rpwmChannel_, 0);
      writePwmDuty(lpwmPin_, lpwmChannel_, -currentSpeed_);
    } else {
      writePwmDuty(rpwmPin_, rpwmChannel_, 0);
      writePwmDuty(lpwmPin_, lpwmChannel_, 0);
    }
  }
};

class Encoder {
  friend class DistanceController;
 public:
  Encoder()
      : pin_(Config::Pins::ENCODER),
        signedPulses_(0),
        direction_(1),
        lastPulseUs_(0),
        latestPeriodUs_(0),
        zeroOffsetPulses_(0),
        lastProcessedPulses_(0),
        filteredVelocityCmS_(0.0f),
        velocitySampleIndex_(0),
        velocitySampleCount_(0) {
    for (float& sample : velocitySamples_) {
      sample = 0.0f;
    }
  }

  void begin(uint8_t pin) {
    pin_ = pin;
    activeInstance_ = this;
    pinMode(pin_, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pin_), handleInterrupt, RISING);
  }

  void update() {
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
        lastPulseUs == 0 ||
        (nowUs - lastPulseUs) >= Config::EncoderConfig::STOP_TIMEOUT_US;

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

    if (periodUs == 0) {
      return;
    }

    if (pulses != lastProcessedPulses_) {
      const int pulseDirection = pulses > lastProcessedPulses_ ? 1 : -1;
      // Average periods before taking the reciprocal; averaging speeds
      // overweights short intervals and produces large false velocity spikes.
      velocitySamples_[velocitySampleIndex_] = pulseDirection * static_cast<float>(periodUs);
      velocitySampleIndex_ =
          (velocitySampleIndex_ + 1) %
          Config::EncoderConfig::VELOCITY_FILTER_SAMPLES;
      if (velocitySampleCount_ <
          Config::EncoderConfig::VELOCITY_FILTER_SAMPLES) {
        velocitySampleCount_++;
      }
      lastProcessedPulses_ = pulses;
    }

    if (velocitySampleCount_ == 0) {
      return;
    }

    float sum = 0.0f;
    for (uint8_t i = 0; i < velocitySampleCount_; ++i) {
      sum += velocitySamples_[i];
    }
    float meanPeriodUs = sum / static_cast<float>(velocitySampleCount_);
    if (velocitySampleCount_ == 3) {
      // Median smooths one isolated interval without throwing away counts
      // or over-weighting its reciprocal. It also avoids count lockout after
      // real speed changes; every edge above the fixed debounce floor counts.
      const float a = velocitySamples_[0], b = velocitySamples_[1], c = velocitySamples_[2];
      meanPeriodUs = max(min(a, b), min(max(a, b), c));
    }
    filteredVelocityCmS_ = Config::EncoderConfig::DISTANCE_PER_PULSE_CM *
                          1000000.0f / meanPeriodUs;
    // If the next pulse is late, bound speed by elapsed pulse time.
    // This follows deceleration between pulses without injecting zero
    // readings and resetting the filter while the wheel is still moving.
    const uint32_t pulseAgeUs = nowUs - lastPulseUs;
    if (pulseAgeUs > fabsf(meanPeriodUs)) {
      const float ageLimitedSpeed =
          Config::EncoderConfig::DISTANCE_PER_PULSE_CM * 1000000.0f /
          static_cast<float>(pulseAgeUs);
      filteredVelocityCmS_ = copysignf(
          min(fabsf(filteredVelocityCmS_), ageLimitedSpeed),
          filteredVelocityCmS_);
    }
  }

  void setDirection(int direction) {
    if (direction > 0) {
      direction_ = 1;
    } else if (direction < 0) {
      direction_ = -1;
    }
  }

  void zero() {
    noInterrupts();
    zeroOffsetPulses_ = signedPulses_;
    interrupts();
  }

  float positionCm() const {
    int32_t pulses;
    noInterrupts();
    pulses = signedPulses_;
    interrupts();
    return static_cast<float>(pulses - zeroOffsetPulses_) *
           Config::EncoderConfig::DISTANCE_PER_PULSE_CM;
  }

  float velocityCmS() const { return filteredVelocityCmS_; }

  bool isStopped() const {
    uint32_t lastPulseUs;
    noInterrupts();
    lastPulseUs = lastPulseUs_;
    interrupts();
    return lastPulseUs == 0 ||
           (micros() - lastPulseUs) >= Config::EncoderConfig::STOP_TIMEOUT_US;
  }

 private:
  static Encoder* activeInstance_;
  static void IRAM_ATTR handleInterrupt();

  void IRAM_ATTR onPulse();

  uint8_t pin_;
  volatile int32_t signedPulses_;
  volatile int8_t direction_;
  volatile uint32_t lastPulseUs_;
  volatile uint32_t latestPeriodUs_;
  int32_t zeroOffsetPulses_;
  int32_t lastProcessedPulses_;
  float filteredVelocityCmS_;
  float velocitySamples_[Config::EncoderConfig::VELOCITY_FILTER_SAMPLES];
  uint8_t velocitySampleIndex_;
  uint8_t velocitySampleCount_;
};

Encoder* Encoder::activeInstance_ = nullptr;

void IRAM_ATTR Encoder::handleInterrupt() {
  if (activeInstance_ != nullptr) {
    activeInstance_->onPulse();
  }
}

void IRAM_ATTR Encoder::onPulse() {
  const uint32_t nowUs = micros();
  if (lastPulseUs_ != 0 &&
      (uint32_t)(nowUs - lastPulseUs_) <
          Config::EncoderConfig::MIN_PULSE_INTERVAL_US) {
    return;
  }

  if (lastPulseUs_ != 0) {
    latestPeriodUs_ = nowUs - lastPulseUs_;
  }
  lastPulseUs_ = nowUs;
  signedPulses_ += direction_;
}

#include "tuning_control.h"
