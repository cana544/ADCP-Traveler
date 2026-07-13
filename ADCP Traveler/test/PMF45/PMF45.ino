#include <Arduino.h>

#define ENCODER_PIN 21
#define SLOTS_PER_REV 50.0f
#define PRINT_INTERVAL_MS 10UL

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
    latestPeriodUs = periodUs;
    lastPulseUs = nowUs;
    totalPulses++;
}

void setup()
{
    Serial.begin(115200);

    pinMode(ENCODER_PIN, INPUT_PULLUP);

    attachInterrupt(
        digitalPinToInterrupt(ENCODER_PIN),
        countPulse,
        RISING);

    Serial.println("Time_s,RPM,Encoder_count");
}

void loop()
{
    static uint32_t lastPrintMs = 0;

    uint32_t nowMs = millis();
    if (nowMs - lastPrintMs < PRINT_INTERVAL_MS)
    {
        return;
    }
    lastPrintMs = nowMs;

    uint32_t pulses;
    uint32_t periodUs;

    noInterrupts();
    pulses = totalPulses;
    periodUs = latestPeriodUs;
    interrupts();

    float rpm = 0.0f;
    if (pulses > 1 && periodUs > 0)
    {
        rpm = 60000000.0f / (SLOTS_PER_REV * periodUs);
    }

    Serial.print(nowMs / 1000.0f, 3);
    Serial.print(",");
    Serial.print(rpm, 4);
    Serial.print(",");
    Serial.println(pulses);
}
