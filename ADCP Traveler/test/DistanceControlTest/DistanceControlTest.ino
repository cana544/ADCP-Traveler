#include <Arduino.h>

// Edit these two values before uploading this standalone test sketch.
constexpr float TARGET_DISTANCE_CM = 50.0f;
constexpr int TARGET_DIRECTION = 1;  // 1 = CW, -1 = CCW
constexpr bool ENABLE_CSV_LOG = true;
constexpr bool ENABLE_SERIAL_PLOTTER = false;
constexpr const char* EVENT_PREFIX = "DC_EVENT,";

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
constexpr uint32_t MIN_PULSE_INTERVAL_US = 6000UL;
constexpr uint8_t VELOCITY_FILTER_SAMPLES = 4;
}  // namespace EncoderConfig

namespace Motion {
constexpr float V_MAX_CM_S = 50.0f;
constexpr float A_MAX_CM_S2 = 25.0f;
constexpr float SHORT_MOVE_DISTANCE_CM = 25.0f;
constexpr float SHORT_MOVE_A_MAX_CM_S2 = 6.0f;
constexpr float SHORT_MOVE_V_MAX_CM_S = 12.0f;
}  // namespace Motion

namespace Control {
constexpr float POSITION_KP = 3.0f;
constexpr float VELOCITY_KP = 13.0f;
constexpr float VELOCITY_KI = 2.0f;
constexpr float VELOCITY_KD = 0.0f;
constexpr float INTEGRAL_ERROR_LIMIT_M = 0.20f;
constexpr float SUPPLY_VOLTAGE = 12.0f;
constexpr float FEEDFORWARD_GAIN_V_PER_M_S = 26.9608f;
constexpr float MAX_VOLTAGE_STEP = 1.25f;
constexpr float CORRECTION_MAX_VOLTAGE_STEP = 0.65f;
constexpr float MIN_EFFECTIVE_VOLTAGE = 2.6f;
constexpr float MIN_TRACKING_VOLTAGE = 2.35f;
constexpr float MIN_SETTLE_VOLTAGE = 1.8f;
constexpr float SETTLE_CREEP_VOLTAGE = 2.2f;
constexpr float MIN_DRIVE_ENABLE_VELOCITY_CM_S = 0.5f;
constexpr float POSITION_TOLERANCE_CM = EncoderConfig::DISTANCE_PER_PULSE_CM;
constexpr float FINAL_POSITION_TOLERANCE_CM =
    EncoderConfig::DISTANCE_PER_PULSE_CM * 0.5f;
constexpr float VELOCITY_TOLERANCE_CM_S = 1.0f;
constexpr uint8_t COMPLETE_CONFIRM_CYCLES = 5;
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
      const float rawVelocityCmS =
          pulseDirection * Config::EncoderConfig::DISTANCE_PER_PULSE_CM *
          1000000.0f / static_cast<float>(periodUs);
  
      velocitySamples_[velocitySampleIndex_] = rawVelocityCmS;
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
    filteredVelocityCmS_ = sum / static_cast<float>(velocitySampleCount_);
    // If the next pulse is late, bound speed by elapsed pulse time.
    // This follows deceleration between pulses without injecting zero
    // readings and resetting the filter while the wheel is still moving.
    const uint32_t pulseAgeUs = nowUs - lastPulseUs;
    if (pulseAgeUs > periodUs) {
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

class MotionProfile {
 public:
  MotionProfile()
      : distanceCm_(0.0f),
        direction_(1),
        tAccel_(0.0f),
        tCruise_(0.0f),
        tTotal_(0.0f),
        vPeakCmS_(0.0f),
        vMaxCmS_(0.0f),
        aMaxCmS2_(0.0f),
        xAccelCm_(0.0f),
        xCruiseCm_(0.0f),
        triangular_(true) {}

  void start(float distanceCm, int direction) {
    distanceCm_ = max(0.0f, distanceCm);
    direction_ = direction >= 0 ? 1 : -1;

    vMaxCmS_ = profileVelocityForDistance(distanceCm_);
    aMaxCmS2_ = profileAccelerationForDistance(distanceCm_);
    const float dCrit = (vMaxCmS_ * vMaxCmS_) / aMaxCmS2_;
    triangular_ = distanceCm_ <= dCrit;

    if (distanceCm_ <= 0.0f) {
      tAccel_ = 0.0f;
      tCruise_ = 0.0f;
      tTotal_ = 0.0f;
      vPeakCmS_ = 0.0f;
      vMaxCmS_ = 0.0f;
      aMaxCmS2_ = 0.0f;
      xAccelCm_ = 0.0f;
      xCruiseCm_ = 0.0f;
      return;
    }

    if (triangular_) {
      tAccel_ = sqrtf(distanceCm_ / aMaxCmS2_);
      tCruise_ = 0.0f;
      tTotal_ = 2.0f * tAccel_;
      vPeakCmS_ = aMaxCmS2_ * tAccel_;
      xAccelCm_ = 0.5f * aMaxCmS2_ * tAccel_ * tAccel_;
      xCruiseCm_ = 0.0f;
    } else {
      tAccel_ = vMaxCmS_ / aMaxCmS2_;
      xAccelCm_ = 0.5f * aMaxCmS2_ * tAccel_ * tAccel_;
      xCruiseCm_ = distanceCm_ - 2.0f * xAccelCm_;
      tCruise_ = xCruiseCm_ / vMaxCmS_;
      tTotal_ = 2.0f * tAccel_ + tCruise_;
      vPeakCmS_ = vMaxCmS_;
    }
  }

  MotionReference sample(float elapsedSeconds) const {
    if (distanceCm_ <= 0.0f || elapsedSeconds <= 0.0f) {
      return {0.0f, 0.0f, distanceCm_ <= 0.0f};
    }

    float position = 0.0f;
    float velocity = 0.0f;
    bool finished = false;

    if (triangular_) {
      if (elapsedSeconds < tAccel_) {
        velocity = aMaxCmS2_ * elapsedSeconds;
        position = 0.5f * aMaxCmS2_ * elapsedSeconds * elapsedSeconds;
      } else if (elapsedSeconds < tTotal_) {
        const float tau = elapsedSeconds - tAccel_;
        velocity = vPeakCmS_ - aMaxCmS2_ * tau;
        position = xAccelCm_ + vPeakCmS_ * tau -
                   0.5f * aMaxCmS2_ * tau * tau;
      } else {
        position = distanceCm_;
        velocity = 0.0f;
        finished = true;
      }
    } else {
      const float tCruiseEnd = tAccel_ + tCruise_;
      if (elapsedSeconds < tAccel_) {
        velocity = aMaxCmS2_ * elapsedSeconds;
        position = 0.5f * aMaxCmS2_ * elapsedSeconds * elapsedSeconds;
      } else if (elapsedSeconds < tCruiseEnd) {
        const float tau = elapsedSeconds - tAccel_;
        velocity = vMaxCmS_;
        position = xAccelCm_ + vMaxCmS_ * tau;
      } else if (elapsedSeconds < tTotal_) {
        const float tau = elapsedSeconds - tCruiseEnd;
        velocity = vMaxCmS_ - aMaxCmS2_ * tau;
        position = xAccelCm_ + xCruiseCm_ +
                   vMaxCmS_ * tau - 0.5f * aMaxCmS2_ * tau * tau;
      } else {
        position = distanceCm_;
        velocity = 0.0f;
        finished = true;
      }
    }

    return {direction_ * position, direction_ * velocity, finished};
  }

 private:
  float distanceCm_;
  int direction_;
  float tAccel_;
  float tCruise_;
  float tTotal_;
  float vPeakCmS_;
  float vMaxCmS_;
  float aMaxCmS2_;
  float xAccelCm_;
  float xCruiseCm_;
  bool triangular_;

  float profileAccelerationForDistance(float distanceCm) const {
    if (distanceCm <= Config::Motion::SHORT_MOVE_DISTANCE_CM) {
      return Config::Motion::SHORT_MOVE_A_MAX_CM_S2;
    }
    return Config::Motion::A_MAX_CM_S2;
  }

  float profileVelocityForDistance(float distanceCm) const {
    if (distanceCm <= Config::Motion::SHORT_MOVE_DISTANCE_CM) {
      return Config::Motion::SHORT_MOVE_V_MAX_CM_S;
    }
    return Config::Motion::V_MAX_CM_S;
  }
};

class DistanceController {
 public:
  enum class State { IDLE, MOVING, SETTLING, REVERSAL_WAIT, COMPLETE };

  DistanceController(Encoder& encoder, MotorController& motor)
      : encoder_(encoder),
        motor_(motor),
        state_(State::IDLE),
        phaseStartUs_(0),
        moveStartMs_(0),
        requestedDistanceCm_(0.0f),
        targetPositionCm_(0.0f),
        phaseStartPositionCm_(0.0f),
        phaseDirection_(1),
        profileTimingStarted_(false),
        correctionPhase_(false),
        commandedVelocityCmS_(0.0f),
        integralErrorM_(0.0f),
        previousVelocityErrorM_S_(0.0f),
        previousEffortVoltage_(0.0f),
        completeConfirmCount_(0) {}

  void beginMove(float distanceCm, int direction) {
    const float moveDistanceCm = fabsf(distanceCm);
    requestedDistanceCm_ = moveDistanceCm;
    targetPositionCm_ =
        encoder_.positionCm() +
        (direction >= 0 ? moveDistanceCm : -moveDistanceCm);
    integralErrorM_ = 0.0f;
    previousVelocityErrorM_S_ = 0.0f;
    commandedVelocityCmS_ = 0.0f;
    completeConfirmCount_ = 0;
    moveStartMs_ = millis();

    if (ENABLE_CSV_LOG) {
      printCsvHeader();
    }
    motor_.setEnabled(true);
    startPhase(moveDistanceCm, direction >= 0 ? 1 : -1);
  }

  void update() {
    if (state_ == State::MOVING || state_ == State::SETTLING) {
      runControlPhase();
      return;
    }

    if (state_ == State::REVERSAL_WAIT) {
      motor_.stop();
      if (encoder_.isStopped()) {
        startCorrectionIfNeeded();
      }
    }
  }

  bool isActive() const {
    return state_ == State::MOVING || state_ == State::SETTLING ||
           state_ == State::REVERSAL_WAIT;
  }

  const char* stateText() const {
    switch (state_) {
      case State::IDLE:
        return "IDLE";
      case State::MOVING:
        return "MOVING";
      case State::SETTLING:
        return "SETTLING";
      case State::REVERSAL_WAIT:
        return "REVERSAL_WAIT";
      case State::COMPLETE:
        return "COMPLETE";
    }
    return "IDLE";
  }

 private:
  Encoder& encoder_;
  MotorController& motor_;
  MotionProfile profile_;
  State state_;
  uint32_t phaseStartUs_;
  uint32_t moveStartMs_;
  float requestedDistanceCm_;
  float targetPositionCm_;
  float phaseStartPositionCm_;
  int phaseDirection_;
  bool profileTimingStarted_;
  bool correctionPhase_;
  float commandedVelocityCmS_;
  float integralErrorM_;
  float previousVelocityErrorM_S_;
  float previousEffortVoltage_;
  uint8_t completeConfirmCount_;

  float quantizeDistanceToPulse(float distanceCm) const {
    distanceCm = fabsf(distanceCm);
    if (distanceCm <= 0.0f) {
      return 0.0f;
    }

    const float pulseCount =
        ceilf(distanceCm / Config::EncoderConfig::DISTANCE_PER_PULSE_CM);
    return max(1.0f, pulseCount) *
           Config::EncoderConfig::DISTANCE_PER_PULSE_CM;
  }

  void startPhase(float distanceCm, int direction,
                  bool correctionPhase = false) {
    (void)distanceCm;
    phaseDirection_ = direction >= 0 ? 1 : -1;
    phaseStartPositionCm_ = encoder_.positionCm();
    phaseStartUs_ = micros();
    profileTimingStarted_ = false;
    correctionPhase_ = correctionPhase;
    commandedVelocityCmS_ = 0.0f;
    integralErrorM_ = 0.0f;
    previousVelocityErrorM_S_ = 0.0f;
    previousEffortVoltage_ = 0.0f;
    completeConfirmCount_ = 0;

    encoder_.setDirection(phaseDirection_);
    state_ = State::MOVING;
  }

  void runControlPhase() {
    const float measuredPositionCm = encoder_.positionCm();
    const float measuredVelocityCmS = encoder_.velocityCmS();
    if (!profileTimingStarted_) {
      if (runBreakawayPhase(measuredPositionCm, measuredVelocityCmS)) {
        return;
      }
      profileTimingStarted_ = true;
      phaseStartUs_ = micros();
    }

    const float targetErrorCm = targetPositionCm_ - measuredPositionCm;
    // Remove drive immediately at the nearest reachable encoder position.
    // Do not ramp through zero and drive past the target while confirming stop.
    if (fabsf(targetErrorCm) <= Config::Control::FINAL_POSITION_TOLERANCE_CM) {
      commandedVelocityCmS_ = 0.0f;
      integralErrorM_ = 0.0f;
      previousEffortVoltage_ = 0.0f;
      motor_.stop();
      state_ = State::SETTLING;
      if (encoder_.isStopped()) {
        if (++completeConfirmCount_ >= Config::Control::COMPLETE_CONFIRM_CYCLES) {
          state_ = State::COMPLETE;
          printEvent("COMPLETE");
        }
      } else {
        completeConfirmCount_ = 0;
      }
      const MotionReference stoppedReference = {0.0f, 0.0f, false};
      if (ENABLE_CSV_LOG) {
        printCsvSample(stoppedReference, measuredPositionCm, measuredVelocityCmS,
                       targetPositionCm_, targetErrorCm, 0.0f, 0.0f, 0.0f,
                       0.0f, 0.0f, 0.0f);
      }
      if (ENABLE_SERIAL_PLOTTER) {
        printPlotterSample(stoppedReference, measuredPositionCm,
                           measuredVelocityCmS, 0.0f);
      }
      return;
    }
    phaseDirection_ = targetErrorCm >= 0.0f ? 1 : -1;
    encoder_.setDirection(phaseDirection_);

    const float dt =
        static_cast<float>(Config::Control::CONTROL_PERIOD_US) * 1.0e-6f;
    float velocityCommandCmS = distanceToGoVelocityCommand(targetErrorCm);
    velocityCommandCmS = rampVelocityCommand(velocityCommandCmS, dt);
    const MotionReference reference = {targetPositionCm_ - phaseStartPositionCm_,
                                      velocityCommandCmS, false};
    const float desiredPositionCm = targetPositionCm_;
    const float positionErrorCm = targetErrorCm;

    const float velocityErrorM_S =
        (velocityCommandCmS - measuredVelocityCmS) / 100.0f;
    integralErrorM_ += velocityErrorM_S * dt;
    integralErrorM_ =
        constrain(integralErrorM_, -Config::Control::INTEGRAL_ERROR_LIMIT_M,
                  Config::Control::INTEGRAL_ERROR_LIMIT_M);
    const float derivativeErrorM_S2 =
        (velocityErrorM_S - previousVelocityErrorM_S_) / dt;
    previousVelocityErrorM_S_ = velocityErrorM_S;

    const float feedbackVoltage =
        Config::Control::VELOCITY_KP * velocityErrorM_S +
        Config::Control::VELOCITY_KI * integralErrorM_ +
        Config::Control::VELOCITY_KD * derivativeErrorM_S2;
    const float feedforwardVoltage =
        Config::Control::FEEDFORWARD_GAIN_V_PER_M_S *
        (velocityCommandCmS / 100.0f);
    float effortVoltage =
        constrain(feedbackVoltage + feedforwardVoltage,
                  -Config::Control::SUPPLY_VOLTAGE,
                  Config::Control::SUPPLY_VOLTAGE);

    if ((phaseDirection_ > 0 && effortVoltage < 0.0f) ||
        (phaseDirection_ < 0 && effortVoltage > 0.0f)) {
      effortVoltage = 0.0f;
    }
    effortVoltage = applyMinimumDriveVoltage(effortVoltage, velocityCommandCmS,
                                             positionErrorCm);
    effortVoltage = limitVoltageStep(effortVoltage);

    motor_.setVoltage(effortVoltage);
    if (ENABLE_CSV_LOG) {
      printCsvSample(reference, measuredPositionCm, measuredVelocityCmS,
                     desiredPositionCm, positionErrorCm, velocityCommandCmS,
                     velocityErrorM_S, derivativeErrorM_S2, feedbackVoltage,
                     feedforwardVoltage, effortVoltage);
    }
    if (ENABLE_SERIAL_PLOTTER) {
      printPlotterSample(reference, measuredPositionCm, measuredVelocityCmS,
                         effortVoltage);
    }

    if (fabsf(targetErrorCm) <= Config::Control::FINAL_POSITION_TOLERANCE_CM &&
        fabsf(measuredVelocityCmS) <=
            Config::Control::VELOCITY_TOLERANCE_CM_S &&
        encoder_.isStopped()) {
      completeConfirmCount_++;
      if (completeConfirmCount_ >= Config::Control::COMPLETE_CONFIRM_CYCLES) {
        motor_.stop();
        state_ = State::COMPLETE;
        printEvent("COMPLETE");
      }
      return;
    }

    completeConfirmCount_ = 0;
    state_ = fabsf(targetErrorCm) <= Config::Control::POSITION_TOLERANCE_CM
                 ? State::SETTLING
                 : State::MOVING;
  }

  bool runBreakawayPhase(float measuredPositionCm,
                         float measuredVelocityCmS) {
    if (fabsf(measuredPositionCm - phaseStartPositionCm_) >=
        Config::EncoderConfig::DISTANCE_PER_PULSE_CM) {
      return false;
    }

    const float effortVoltage =
        phaseDirection_ > 0 ? Config::Control::MIN_EFFECTIVE_VOLTAGE
                            : -Config::Control::MIN_EFFECTIVE_VOLTAGE;
    const MotionReference reference = {0.0f, 0.0f, false};

    motor_.setVoltage(effortVoltage);
    previousEffortVoltage_ = effortVoltage;
    if (ENABLE_CSV_LOG) {
      printCsvSample(reference, measuredPositionCm, measuredVelocityCmS,
                     phaseStartPositionCm_, 0.0f, 0.0f, 0.0f, 0.0f,
                     0.0f, 0.0f, effortVoltage);
    }
    if (ENABLE_SERIAL_PLOTTER) {
      printPlotterSample(reference, measuredPositionCm, measuredVelocityCmS,
                         effortVoltage);
    }
    state_ = State::MOVING;
    return true;
  }

  float distanceToGoVelocityCommand(float targetErrorCm) const {
    const float distanceRemainingCm = fabsf(targetErrorCm);
    if (distanceRemainingCm <= Config::Control::FINAL_POSITION_TOLERANCE_CM) {
      return 0.0f;
    }

    const float aMax = profileAccelerationForMove();
    const float vMax = profileVelocityForMove();
    const float brakingLimitedVelocity = sqrtf(2.0f * aMax *
                                               distanceRemainingCm);
    const float targetVelocityCmS = min(vMax, brakingLimitedVelocity);
    return targetErrorCm >= 0.0f ? targetVelocityCmS : -targetVelocityCmS;
  }

  float rampVelocityCommand(float targetVelocityCmS, float dt) {
    const float aMax = profileAccelerationForMove();
    const float maxDelta = aMax * dt;
    commandedVelocityCmS_ =
        constrain(targetVelocityCmS, commandedVelocityCmS_ - maxDelta,
                  commandedVelocityCmS_ + maxDelta);
    return commandedVelocityCmS_;
  }

  float profileAccelerationForDistance(float distanceCm) const {
    if (distanceCm <= Config::Motion::SHORT_MOVE_DISTANCE_CM) {
      return Config::Motion::SHORT_MOVE_A_MAX_CM_S2;
    }
    return Config::Motion::A_MAX_CM_S2;
  }

  float profileVelocityForDistance(float distanceCm) const {
    if (distanceCm <= Config::Motion::SHORT_MOVE_DISTANCE_CM) {
      return Config::Motion::SHORT_MOVE_V_MAX_CM_S;
    }
    return Config::Motion::V_MAX_CM_S;
  }

  float profileAccelerationForMove() const {
    return profileAccelerationForDistance(requestedDistanceCm_);
  }

  float profileVelocityForMove() const {
    return profileVelocityForDistance(requestedDistanceCm_);
  }

  float limitVoltageStep(float effortVoltage) {
    const float maxStep =
        correctionPhase_ ? Config::Control::CORRECTION_MAX_VOLTAGE_STEP
                         : Config::Control::MAX_VOLTAGE_STEP;
    const float limited = constrain(
        effortVoltage, previousEffortVoltage_ - maxStep,
        previousEffortVoltage_ + maxStep);
    previousEffortVoltage_ = limited;
    return limited;
  }

  float applyMinimumDriveVoltage(float effortVoltage,
                                 float velocityCommandCmS,
                                 float positionErrorCm) const {
    (void)positionErrorCm;
    if (fabsf(velocityCommandCmS) < Config::Control::VELOCITY_TOLERANCE_CM_S ||
        fabsf(effortVoltage) < 0.001f) {
      return effortVoltage;
    }

    if (fabsf(velocityCommandCmS) <
        Config::Control::MIN_DRIVE_ENABLE_VELOCITY_CM_S) {
      return effortVoltage;
    }

    const float minimumDriveVoltage =
        minimumDriveVoltageForCommand(velocityCommandCmS);
    if (phaseDirection_ > 0 &&
        effortVoltage < minimumDriveVoltage) {
      return minimumDriveVoltage;
    }
    if (phaseDirection_ < 0 &&
        effortVoltage > -minimumDriveVoltage) {
      return -minimumDriveVoltage;
    }

    return effortVoltage;
  }

  float minimumDriveVoltageForCommand(float velocityCommandCmS) const {
    if (correctionPhase_ ||
        fabsf(velocityCommandCmS) <= Config::Motion::SHORT_MOVE_V_MAX_CM_S) {
      return Config::Control::MIN_TRACKING_VOLTAGE;
    }
    return Config::Control::MIN_EFFECTIVE_VOLTAGE;
  }

  float applySignedMinimumVoltage(float effortVoltage,
                                  float minimumMagnitude) const {
    if (effortVoltage > 0.0f) {
      return minimumMagnitude;
    }
    if (effortVoltage < 0.0f) {
      return -minimumMagnitude;
    }
    return effortVoltage;
  }

  void applyFinalCreepVoltage(float targetErrorCm) {
    const int creepDirection = targetErrorCm >= 0.0f ? 1 : -1;
    phaseDirection_ = creepDirection;
    encoder_.setDirection(creepDirection);

    float effortVoltage =
        creepDirection > 0 ? Config::Control::SETTLE_CREEP_VOLTAGE
                           : -Config::Control::SETTLE_CREEP_VOLTAGE;
    effortVoltage = limitVoltageStep(effortVoltage);
    motor_.setVoltage(effortVoltage);
    state_ = State::SETTLING;
  }

  void startCorrectionIfNeeded() {
    const float errorCm = targetPositionCm_ - encoder_.positionCm();
    if (fabsf(errorCm) <= Config::Control::FINAL_POSITION_TOLERANCE_CM) {
      state_ = State::COMPLETE;
      printEvent("COMPLETE");
      return;
    }

    printEventValue("CORRECTION", errorCm);
    startPhase(fabsf(errorCm), errorCm >= 0.0f ? 1 : -1, true);
  }

  void printEvent(const char* eventName) const {
    if (ENABLE_CSV_LOG) {
      return;
    }
    Serial.print(EVENT_PREFIX);
    Serial.println(eventName);
  }

  void printEventValue(const char* eventName, float value) const {
    if (ENABLE_CSV_LOG) {
      return;
    }
    Serial.print(EVENT_PREFIX);
    Serial.print(eventName);
    Serial.print(",");
    Serial.println(value, 3);
  }

  void printCsvHeader() const {
    Serial.println(
        "time_s,distance_cm,velocity_cm_s,effort_v,pwm,ref_velocity_cm_s,"
        "velocity_cmd_cm_s");
  }

  void printCsvSample(const MotionReference& reference, float measuredPositionCm,
                      float measuredVelocityCmS, float desiredPositionCm,
                      float positionErrorCm, float velocityCommandCmS,
                      float velocityErrorM_S, float derivativeErrorM_S2,
                      float feedbackVoltage, float feedforwardVoltage,
                      float effortVoltage) const {
    (void)reference;
    (void)desiredPositionCm;
    (void)positionErrorCm;
    (void)velocityCommandCmS;
    (void)velocityErrorM_S;
    (void)derivativeErrorM_S2;
    (void)feedbackVoltage;
    (void)feedforwardVoltage;
    (void)effortVoltage;

    const float timeSeconds = static_cast<float>(millis() - moveStartMs_) * 0.001f;

    Serial.print(timeSeconds, 3);
    Serial.print(',');
    Serial.print(measuredPositionCm, 3);
    Serial.print(',');
    Serial.print(measuredVelocityCmS, 3);
    Serial.print(',');
    Serial.print(effortVoltage, 4);
    Serial.print(',');
    Serial.print(motor_.currentSpeed());
    Serial.print(',');
    Serial.print(reference.velocityCmS, 3);
    Serial.print(',');
    Serial.println(velocityCommandCmS, 3);
  }

  void printPlotterSample(const MotionReference& reference,
                          float measuredPositionCm, float measuredVelocityCmS,
                          float effortVoltage) const {
    Serial.print("plot_actual_pos_cm:");
    Serial.print(measuredPositionCm, 3);
    Serial.print(" plot_target_pos_cm:");
    Serial.print(targetPositionCm_, 3);
    Serial.print(" plot_actual_vel_cm_s:");
    Serial.print(measuredVelocityCmS, 3);
    Serial.print(" plot_ref_vel_cm_s:");
    Serial.print(reference.velocityCmS, 3);
    Serial.print(" plot_effort_v:");
    Serial.print(effortVoltage, 4);
    Serial.print(" plot_pwm:");
    Serial.println(motor_.currentSpeed());
  }
};

MotorController motorController;
Encoder encoder;
DistanceController distanceController(encoder, motorController);
uint32_t lastControlUs = 0;
bool testStarted = false;

void setup() {
  Serial.begin(115200);
  delay(1000);

  motorController.begin(Config::Pins::MOTOR_RPWM, Config::Pins::MOTOR_LPWM,
                        Config::Pins::MOTOR_REN, Config::Pins::MOTOR_LEN);
  encoder.begin(Config::Pins::ENCODER);
  encoder.zero();
  motorController.stop();

  if (!ENABLE_CSV_LOG) {
    Serial.print(EVENT_PREFIX);
    Serial.println("READY");
    Serial.print(EVENT_PREFIX);
    Serial.print("TARGET_CM,");
    Serial.println(TARGET_DISTANCE_CM, 3);
    Serial.print(EVENT_PREFIX);
    Serial.print("DIRECTION,");
    Serial.println(TARGET_DIRECTION >= 0 ? "CW" : "CCW");
  }

  delay(1500);
  lastControlUs = micros();
  distanceController.beginMove(TARGET_DISTANCE_CM, TARGET_DIRECTION);
  testStarted = true;
}

void loop() {
  const uint32_t nowUs = micros();
  if ((uint32_t)(nowUs - lastControlUs) >= Config::Control::CONTROL_PERIOD_US) {
    lastControlUs += Config::Control::CONTROL_PERIOD_US;
    encoder.update();
    if (testStarted && distanceController.isActive()) {
      distanceController.update();
    }
  }
}
