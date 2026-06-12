#include <Arduino.h>

#define ENCODER_PIN 21
#define SLOTS_PER_REV 50.0f
#define REPORT_INTERVAL_MS 100

// Reject pulses closer than 2 ms.
// This still allows speeds far above your motor's expected speed.
#define MIN_PULSE_PERIOD_US 2000UL

// Report zero if no pulse is received for one second.
#define STOP_TIMEOUT_US 1000000UL

volatile uint32_t totalPulses = 0;
volatile uint32_t lastPulseUs = 0;
volatile uint32_t latestPeriodUs = 0;

void IRAM_ATTR countPulse()
{
  uint32_t nowUs = micros();

  if (lastPulseUs == 0)
  {
    lastPulseUs = nowUs;
    totalPulses++;
    return;
  }

  uint32_t periodUs = nowUs - lastPulseUs;

  if (periodUs >= MIN_PULSE_PERIOD_US)
  {
    latestPeriodUs = periodUs;
    lastPulseUs = nowUs;
    totalPulses++;
  }
}

void setup()
{
  Serial.begin(115200);

  pinMode(ENCODER_PIN, INPUT_PULLUP);

  attachInterrupt(
      digitalPinToInterrupt(ENCODER_PIN),
      countPulse,
      RISING);

  Serial.println("PM-F45 RPM measurement starting");
}

void loop()
{
  static uint32_t lastReportMs = millis();
  static float filteredRPM = 0.0f;

  uint32_t nowMs = millis();

  if (nowMs - lastReportMs >= REPORT_INTERVAL_MS)
  {
    uint32_t pulses;
    uint32_t periodUs;
    uint32_t pulseTimeUs;

    noInterrupts();
    pulses = totalPulses;
    periodUs = latestPeriodUs;
    pulseTimeUs = lastPulseUs;
    interrupts();

    uint32_t timeSincePulseUs = micros() - pulseTimeUs;

    if (pulseTimeUs == 0 || timeSincePulseUs > STOP_TIMEOUT_US)
    {
      filteredRPM = 0.0f;
    }
    else if (periodUs > 0)
    {
      float measuredRPM =
          60000000.0f /
          (SLOTS_PER_REV * periodUs);

      // Light smoothing
      const float alpha = 0.25f;

      if (filteredRPM == 0.0f)
      {
        filteredRPM = measuredRPM;
      }
      else
      {
        filteredRPM =
            alpha * measuredRPM +
            (1.0f - alpha) * filteredRPM;
      }
    }

    Serial.print("Total pulses: ");
    Serial.print(pulses);

    Serial.print("    RPM: ");
    Serial.println(filteredRPM, 1);

    lastReportMs = nowMs;
  }
}