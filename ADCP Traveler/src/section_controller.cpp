#include "section_controller.h"
#include <cmath>
#include "config.h"

SectionController::SectionController(Encoder& encoder, MotorController& motor,
                                     DistanceController& distance)
    : encoder_(encoder), motor_(motor), distance_(distance) {}

bool SectionController::reject(const char* message) { error_ = message; return false; }

bool SectionController::stationary() const {
  return !distance_.isActive() && motor_.currentSpeed() == 0 &&
      encoder_.isStopped(Config::Encoder::DISTANCE_STOP_TIMEOUT_US);
}

bool SectionController::startScan(int direction) {
  if (stage_ != Stage::PRE_SCAN || !stationary() || (direction != -1 && direction != 1))
    return reject("Select LB or RB and wait for the traveller to stop.");
  if (!motor_.isEnabled()) return reject("Enable Traveller First");
  scanDirection_ = direction;
  scanStartCm_ = scanEndCm_ = encoder_.positionCm();
  scanStartUs_ = micros();
  scanSettling_ = executionStarted_ = false;
  completed_ = plan_.count = 0;
  error_ = "";
  encoder_.setDistanceMode(false);
  encoder_.setDirection(direction);
  motor_.stop();
  stage_ = Stage::SCANNING;
  return true;
}

float SectionController::spanCm() const {
  const float endpoint = stage_ == Stage::SCANNING || scanSettling_
      ? encoder_.positionCm() : scanEndCm_;
  return std::fabs(endpoint - scanStartCm_);
}

void SectionController::stop() {
  if (stage_ == Stage::SCANNING) {
    motor_.stop();
    scanEndCm_ = encoder_.positionCm();
    scanSettling_ = true;
    stage_ = Stage::CONFIGURE;
    error_ = "";
  } else if (stage_ == Stage::MOVING_SECTION) {
    distance_.cancel();
    stage_ = Stage::READY_FOR_SECTION;
    error_ = "Section paused. Press GO to continue the remaining distance.";
  }
}

bool SectionController::configure(double count) {
  if ((stage_ != Stage::CONFIGURE && stage_ != Stage::READY_FOR_SECTION) || executionStarted_)
    return reject("Section count is locked once gauging begins.");
  if (!stationary()) return reject("Wait for the traveller to stop.");
  update(); // Finalize the scan coast even between control-loop ticks.
  plan_.count = 0;
  stage_ = Stage::CONFIGURE;
  if (!std::isfinite(count) || count != std::floor(count) ||
      count < SectionPlan::MIN_COUNT || count > SectionPlan::MAX_COUNT)
    return reject("Enter a valid number of sections (6 to 60).");
  if (spanCm() <= 0) return reject("Run a scan first to measure the span.");
  if (!plan_.generate(spanCm(), static_cast<int>(count)))
    return reject("Too many sections for this span. Minimum section width is 10 cm. Reduce the number of sections.");
  completed_ = 0;
  stage_ = Stage::READY_FOR_SECTION;
  error_ = "";
  return true;
}

int SectionController::nextSection() const {
  if (plan_.count == 0 || completed_ >= plan_.count) return 0;
  return scanDirection_ == 1 ? plan_.count - completed_ : completed_ + 1;
}

int SectionController::lastCompletedSection() const {
  if (completed_ == 0) return 0;
  return scanDirection_ == 1 ? plan_.count - completed_ + 1 : completed_;
}

float SectionController::targetCm() const {
  const int section = nextSection();
  if (!section) return encoder_.positionCm();
  const float leftBank = scanDirection_ == 1 ? scanEndCm_ - spanCm() : scanEndCm_;
  return leftBank + plan_.boundariesCm[scanDirection_ == 1 ? section - 1 : section];
}

float SectionController::remainingCm() const {
  if (!nextSection()) return 0;
  return std::fmax(0.0f, -scanDirection_ * (targetCm() - encoder_.positionCm()));
}

void SectionController::completeSection() {
  ++completed_;
  stage_ = completed_ == plan_.count ? Stage::FINISHED : Stage::READY_FOR_SECTION;
  error_ = "";
}

bool SectionController::go() {
  if (stage_ != Stage::READY_FOR_SECTION || !nextSection())
    return reject(stage_ == Stage::FINISHED ? "All sections completed. Start a new scan to run again."
                                          : "Run a scan and enter a valid number of sections first.");
  if (!stationary()) return reject("Wait for the traveller to stop.");
  const float remaining = -scanDirection_ * (targetCm() - encoder_.positionCm());
  if (remaining < -Config::Control::POSITION_TOLERANCE_CM)
    return reject("Traveller passed the section endpoint. Start a new scan.");
  executionStarted_ = true;
  if (remaining <= Config::Control::POSITION_TOLERANCE_CM) {
    // STOP itself never completes a section. A later GO confirms a stationary endpoint.
    completeSection();
    return true;
  }
  distance_.beginMove(remaining, -scanDirection_);
  if (!distance_.isActive()) return reject("Unable to start section move.");
  stage_ = Stage::MOVING_SECTION;
  error_ = "";
  return true;
}

bool SectionController::newScan() {
  if (active() || !stationary()) return reject("Stop the traveller before starting a new scan.");
  plan_.count = completed_ = 0;
  scanStartCm_ = scanEndCm_ = encoder_.positionCm();
  scanSettling_ = executionStarted_ = false;
  stage_ = Stage::PRE_SCAN;
  error_ = "";
  return true;
}

void SectionController::invalidate() {
  stop();
  plan_.count = completed_ = 0;
  scanStartCm_ = scanEndCm_ = encoder_.positionCm();
  scanSettling_ = executionStarted_ = false;
  stage_ = Stage::PRE_SCAN;
  error_ = "";
}

void SectionController::update() {
  if (stage_ == Stage::SCANNING) {
    if (!motor_.isEnabled()) { stop(); return; }
    const float fraction = std::fmin(1.0f, static_cast<float>(micros() - scanStartUs_) /
        Config::Section::SCAN_RAMP_DURATION_US);
    const float smooth = fraction * fraction * (3 - 2 * fraction);
    const int speed = scanDirection_ * static_cast<int>(std::round(255 * smooth));
    if (motor_.currentSpeed() != speed) motor_.setSpeed(speed);
  }
  if (scanSettling_) {
    scanEndCm_ = encoder_.positionCm();
    if (stationary()) scanSettling_ = false;
  }
  if (stage_ == Stage::MOVING_SECTION && !distance_.isActive()) {
    if (distance_.state() == DistanceController::State::COMPLETE) completeSection();
    else {
      stage_ = Stage::READY_FOR_SECTION;
      error_ = "Section stopped before completion. Check the traveller, then press GO to resume.";
    }
  }
}

const char* SectionController::stageText() const {
  switch (stage_) {
    case Stage::PRE_SCAN: return "PRE_SCAN";
    case Stage::SCANNING: return "SCANNING";
    case Stage::CONFIGURE: return "CONFIGURE";
    case Stage::READY_FOR_SECTION: return "READY_FOR_SECTION";
    case Stage::MOVING_SECTION: return "MOVING_SECTION";
    case Stage::FINISHED: return "FINISHED";
  }
  return "PRE_SCAN";
}

const char* SectionController::stateText() const {
  if (stage_ == Stage::SCANNING) return "SCANNING";
  if (stage_ == Stage::MOVING_SECTION) return "MOVING";
  if (stage_ == Stage::FINISHED) return "FINISHED";
  return "STOPPED";
}
