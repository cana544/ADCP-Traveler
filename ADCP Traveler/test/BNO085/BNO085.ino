#include <Adafruit_BNO08x.h>

// ESP32 UART2 pins
#define BNO_RX_PIN 16   // ESP32 RX2, connect to BNO085 SDA
#define BNO_TX_PIN 17   // ESP32 TX2, connect to BNO085 SCL

#define BNO08X_RESET -1

Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;

const unsigned long SAMPLE_PERIOD_MS = 20;
const unsigned long RECORD_DURATION_MS = 15000;
const unsigned long GYRO_CALIBRATION_MS = 2000;
const float ACCEL_FILTER_ALPHA = 0.15;
const float GYRO_DEADBAND_RADPS = 0.003;

float accelX = 0, accelY = 0, accelZ = 0;
float linX = 0, linY = 0, linZ = 0;
float gyroX = 0, gyroY = 0, gyroZ = 0;
float angleX = 0, angleY = 0, angleZ = 0;
float gyroBiasX = 0, gyroBiasY = 0, gyroBiasZ = 0;

unsigned long calibrationStartMs = 0;
unsigned long recordStartMs = 0;
unsigned long lastSampleMs = 0;
unsigned long lastGyroMicros = 0;
unsigned int gyroCalibrationSamples = 0;
bool recordingStarted = false;
bool recordingFinished = false;
bool gyroCalibrated = false;
bool accelFilterReady = false;
bool linFilterReady = false;

float lowPassFilter(float previousValue, float newValue) {
  return previousValue + ACCEL_FILTER_ALPHA * (newValue - previousValue);
}

float removeSmallGyroDrift(float gyroValue) {
  if (gyroValue > -GYRO_DEADBAND_RADPS && gyroValue < GYRO_DEADBAND_RADPS) {
    return 0.0;
  }
  return gyroValue;
}

void startRecording() {
  angleX = 0;
  angleY = 0;
  angleZ = 0;
  lastGyroMicros = 0;
  recordStartMs = millis();
  lastSampleMs = recordStartMs;
  recordingStarted = true;

  // CSV header. Copy all serial output into BNO085_data.csv.
  Serial.println("Time_s,AccelX,AccelY,AccelZ,LinX,LinY,LinZ,AngleX_rad,AngleY_rad,AngleZ_rad");
}

void setReports() {
  bno08x.enableReport(SH2_ACCELEROMETER);
  bno08x.enableReport(SH2_LINEAR_ACCELERATION);
  bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // UART2 for BNO085
  Serial2.begin(115200, SERIAL_8N1, BNO_RX_PIN, BNO_TX_PIN);

  if (!bno08x.begin_UART(&Serial2)) {
    Serial.println("Failed to find BNO085 over UART");
    while (1) {
      delay(10);
    }
  }

  setReports();

  calibrationStartMs = millis();
}

void loop() {
  if (recordingFinished) {
    return;
  }

  if (bno08x.wasReset()) {
    recordingStarted = false;
    recordingFinished = false;
    gyroCalibrated = false;
    gyroCalibrationSamples = 0;
    gyroBiasX = 0;
    gyroBiasY = 0;
    gyroBiasZ = 0;
    calibrationStartMs = millis();
    setReports();
  }

  if (bno08x.getSensorEvent(&sensorValue)) {
    switch (sensorValue.sensorId) {
      case SH2_ACCELEROMETER:
        if (!accelFilterReady) {
          accelX = sensorValue.un.accelerometer.x;
          accelY = sensorValue.un.accelerometer.y;
          accelZ = sensorValue.un.accelerometer.z;
          accelFilterReady = true;
        } else {
          accelX = lowPassFilter(accelX, sensorValue.un.accelerometer.x);
          accelY = lowPassFilter(accelY, sensorValue.un.accelerometer.y);
          accelZ = lowPassFilter(accelZ, sensorValue.un.accelerometer.z);
        }
        break;

      case SH2_LINEAR_ACCELERATION:
        if (!linFilterReady) {
          linX = sensorValue.un.linearAcceleration.x;
          linY = sensorValue.un.linearAcceleration.y;
          linZ = sensorValue.un.linearAcceleration.z;
          linFilterReady = true;
        } else {
          linX = lowPassFilter(linX, sensorValue.un.linearAcceleration.x);
          linY = lowPassFilter(linY, sensorValue.un.linearAcceleration.y);
          linZ = lowPassFilter(linZ, sensorValue.un.linearAcceleration.z);
        }
        break;

      case SH2_GYROSCOPE_CALIBRATED:
        gyroX = sensorValue.un.gyroscope.x;
        gyroY = sensorValue.un.gyroscope.y;
        gyroZ = sensorValue.un.gyroscope.z;

        if (!gyroCalibrated) {
          gyroBiasX += gyroX;
          gyroBiasY += gyroY;
          gyroBiasZ += gyroZ;
          gyroCalibrationSamples++;
          break;
        }

        if (recordingStarted) {
          unsigned long nowGyroMicros = micros();
          if (lastGyroMicros != 0) {
            float dt = (nowGyroMicros - lastGyroMicros) / 1000000.0;
            float correctedGyroX = removeSmallGyroDrift(gyroX - gyroBiasX);
            float correctedGyroY = removeSmallGyroDrift(gyroY - gyroBiasY);
            float correctedGyroZ = removeSmallGyroDrift(gyroZ - gyroBiasZ);

            angleX += correctedGyroX * dt;
            angleY += correctedGyroY * dt;
            angleZ += correctedGyroZ * dt;
          }
          lastGyroMicros = nowGyroMicros;
        }
        break;
    }
  }

  unsigned long nowMs = millis();
  if (!gyroCalibrated) {
    if (nowMs - calibrationStartMs >= GYRO_CALIBRATION_MS && gyroCalibrationSamples > 0) {
      gyroBiasX /= gyroCalibrationSamples;
      gyroBiasY /= gyroCalibrationSamples;
      gyroBiasZ /= gyroCalibrationSamples;
      gyroCalibrated = true;
      startRecording();
    }
    return;
  }

  if (!recordingStarted) {
    return;
  }

  if (nowMs - recordStartMs >= RECORD_DURATION_MS) {
    recordingFinished = true;
    Serial.println("END");
    return;
  }

  if (nowMs - lastSampleMs < SAMPLE_PERIOD_MS) {
    return;
  }

  lastSampleMs += SAMPLE_PERIOD_MS;

  Serial.print((nowMs - recordStartMs) / 1000.0, 3);
  Serial.print(',');
  Serial.print(accelX);
  Serial.print(',');
  Serial.print(accelY);
  Serial.print(',');
  Serial.print(accelZ);
  Serial.print(',');

  Serial.print(linX);
  Serial.print(',');
  Serial.print(linY);
  Serial.print(',');
  Serial.print(linZ);
  Serial.print(',');

  Serial.print(angleX, 6);
  Serial.print(',');
  Serial.print(angleY, 6);
  Serial.print(',');
  Serial.println(angleZ, 6);
}
