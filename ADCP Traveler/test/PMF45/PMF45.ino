#include <Arduino.h>

#define ENCODER_PIN 21
#define SLOTS_PER_REV 50.0f

#define MIN_PULSE_PERIOD_US 2000UL
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

    Serial.println("Time_s,Raw_RPM,Pulse_count");
}

void loop()
{
    static uint32_t lastProcessedPulse = 0;

    uint32_t pulses;
    uint32_t periodUs;
    uint32_t pulseTimeUs;

    noInterrupts();
    pulses = totalPulses;
    periodUs = latestPeriodUs;
    pulseTimeUs = lastPulseUs;
    interrupts();

    // Only calculate speed when a new pulse has arrived
    if (pulses != lastProcessedPulse && periodUs > 0)
    {
        float rawRPM =
            60000000.0f /
            (SLOTS_PER_REV * periodUs);

        float timeSeconds = pulseTimeUs / 1000000.0f;

        Serial.print(timeSeconds, 6);
        Serial.print(",");
        Serial.print(rawRPM, 4);
        Serial.print(",");
        Serial.println(pulses);

        lastProcessedPulse = pulses;
    }
}