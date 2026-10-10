#include "battery_monitor.h"
#include "config.h"
#include <cmath>

float BatteryMonitor::voltageFromMillivolts(float millivolts) {
  return millivolts * 0.001f *
      (Config::Battery::R1_OHMS + Config::Battery::R2_OHMS) /
      Config::Battery::R2_OHMS * Config::Battery::ADC_CORRECTION;
}

float BatteryMonitor::estimatePercent(float voltage) {
  const auto& table = Config::Battery::CHARGE_TABLE;
  constexpr size_t count = sizeof(table) / sizeof(table[0]);
  if (!std::isfinite(voltage) || voltage <= table[0].voltage) return 0;
  for (size_t i = 1; i < count; ++i) {
    if (voltage <= table[i].voltage) {
      const float fraction = (voltage - table[i-1].voltage) /
          (table[i].voltage - table[i-1].voltage);
      const float percent = table[i-1].percent + fraction *
          (table[i].percent - table[i-1].percent);
      return fminf(100.0f, fmaxf(0.0f, percent));
    }
  }
  return 100;
}

void BatteryMonitor::begin() {
  pinMode(Config::Pins::BATTERY_ADC, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(Config::Pins::BATTERY_ADC, ADC_11db);
  valid_ = false;
  sampling_ = false;
  lastCycleMs_ = millis() - Config::Battery::SAMPLE_PERIOD_MS;
}

void BatteryMonitor::update() {
  const uint32_t now = millis();
  if (!sampling_) {
    if (uint32_t(now - lastCycleMs_) < Config::Battery::SAMPLE_PERIOD_MS) return;
    lastCycleMs_ = now;
    lastSampleMs_ = now - Config::Battery::SAMPLE_SPACING_MS;
    samples_ = 0;
    sumMv_ = 0;
    invalidSample_ = false;
    sampling_ = true;
  }
  if (uint32_t(now - lastSampleMs_) < Config::Battery::SAMPLE_SPACING_MS) return;
  lastSampleMs_ = now;
  const uint32_t mv = analogReadMilliVolts(Config::Pins::BATTERY_ADC);
  const float sampleVoltage = voltageFromMillivolts(mv);
  invalidSample_ |= mv >= Config::Battery::MAX_ADC_MV ||
      !std::isfinite(sampleVoltage) ||
      sampleVoltage < Config::Battery::MIN_VALID_VOLTAGE ||
      sampleVoltage > Config::Battery::MAX_VALID_VOLTAGE;
  sumMv_ += mv;
  if (++samples_ < Config::Battery::SAMPLE_COUNT) return;
  sampling_ = false;
  if (invalidSample_) {
    valid_ = false;
    voltage_ = 0;  // Never exposed as a numeric reading while invalid.
    return;
  }
  const float measured = voltageFromMillivolts(float(sumMv_) / samples_);
  voltage_ = valid_ ? voltage_ + Config::Battery::FILTER_ALPHA * (measured - voltage_)
                    : measured;
  valid_ = true;
}
