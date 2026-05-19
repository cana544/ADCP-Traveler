#include <Adafruit_BNO08x.h>
#include <math.h>

// ESP32 UART2 pins
#define BNO_RX_PIN 16   // ESP32 RX2, connect to BNO085 SDA
#define BNO_TX_PIN 17   // ESP32 TX2, connect to BNO085 SCL

#define BNO08X_RESET -1

Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;

const unsigned long SAMPLE_PERIOD_MS = 20;
const uint32_t SENSOR_REPORT_INTERVAL_US = 10000;
const unsigned long RECORD_DURATION_MS = 3600000;
const unsigned long CALIBRATION_MS = 2000;
const float ACCEL_FILTER_ALPHA = 0.1;
const float GYRO_DEADBAND_RADPS = 0.003;
const float LINEAR_ACCEL_DEADBAND_MS2 = 0.015;
const float STILL_ACCEL_THRESHOLD_MS2 = 0.04;
const float STILL_GYRO_THRESHOLD_RADPS = 0.02;
const float VELOCITY_ZERO_THRESHOLD_MS = 0.002;
const float VELOCITY_DECAY_WHEN_STILL = 0.75;
const unsigned int STILL_SAMPLE_LIMIT = 8;
const float GYRO_KALMAN_PROCESS_NOISE = 0.00005;
const float GYRO_KALMAN_MEASUREMENT_NOISE = 0.0008;
const float LIN_KALMAN_PROCESS_NOISE = 0.00012;
const float LIN_KALMAN_MEASUREMENT_NOISE = 0.025;
const float LIN_KALMAN_GYRO_NOISE_GAIN = 0.25;

struct Kalman1D {
  float estimate;
  float errorCovariance;
  bool initialized;

  Kalman1D() : estimate(0), errorCovariance(1), initialized(false) {}

  void reset() {
    estimate = 0;
    errorCovariance = 1;
    initialized = false;
  }

  float update(float measurement, float processNoise, float measurementNoise) {
    if (!initialized) {
      estimate = measurement;
      errorCovariance = measurementNoise;
      initialized = true;
      return estimate;
    }

    errorCovariance += processNoise;
    float kalmanGain = errorCovariance / (errorCovariance + measurementNoise);
    estimate += kalmanGain * (measurement - estimate);
    errorCovariance *= (1.0 - kalmanGain);
    return estimate;
  }
};

float rawAccelX = 0, rawAccelY = 0, rawAccelZ = 0;
float accelX = 0, accelY = 0, accelZ = 0;
float linX = 0, linY = 0, linZ = 0;
float gyroX = 0, gyroY = 0, gyroZ = 0;
float correctedGyroX = 0, correctedGyroY = 0, correctedGyroZ = 0;
float angleX = 0, angleY = 0, angleZ = 0;
float gyroBiasX = 0, gyroBiasY = 0, gyroBiasZ = 0;
float linBiasX = 0, linBiasY = 0, linBiasZ = 0;
float velX = 0, velY = 0, velZ = 0;
float dispX = 0, dispY = 0, dispZ = 0;
float prevLinX = 0, prevLinY = 0, prevLinZ = 0;
Kalman1D gyroKalmanX, gyroKalmanY, gyroKalmanZ;
Kalman1D linKalmanX, linKalmanY, linKalmanZ;

unsigned long calibrationStartMs = 0;
unsigned long recordStartMs = 0;
unsigned long lastSampleMs = 0;
unsigned long lastGyroMicros = 0;
unsigned long lastLinMicros = 0;
unsigned int gyroCalibrationSamples = 0;
unsigned int linCalibrationSamples = 0;
unsigned int stillSamples = 0;
bool recordingStarted = false;
bool recordingFinished = false;
bool calibrated = false;
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

float removeSmallLinearDrift(float accelValue) {
  if (accelValue > -LINEAR_ACCEL_DEADBAND_MS2 && accelValue < LINEAR_ACCEL_DEADBAND_MS2) {
    return 0.0;
  }
  return accelValue;
}

void dampVelocity(float &velocity) {
  velocity *= VELOCITY_DECAY_WHEN_STILL;
  if (velocity > -VELOCITY_ZERO_THRESHOLD_MS && velocity < VELOCITY_ZERO_THRESHOLD_MS) {
    velocity = 0.0;
  }
}

bool isStill() {
  return fabsf(linX) < STILL_ACCEL_THRESHOLD_MS2 &&
         fabsf(linY) < STILL_ACCEL_THRESHOLD_MS2 &&
         fabsf(linZ) < STILL_ACCEL_THRESHOLD_MS2 &&
         fabsf(correctedGyroX) < STILL_GYRO_THRESHOLD_RADPS &&
         fabsf(correctedGyroY) < STILL_GYRO_THRESHOLD_RADPS &&
         fabsf(correctedGyroZ) < STILL_GYRO_THRESHOLD_RADPS;
}

void startRecording() {
  angleX = 0;
  angleY = 0;
  angleZ = 0;
  velX = 0;
  velY = 0;
  velZ = 0;
  dispX = 0;
  dispY = 0;
  dispZ = 0;
  prevLinX = 0;
  prevLinY = 0;
  prevLinZ = 0;
  stillSamples = 0;
  lastGyroMicros = 0;
  lastLinMicros = 0;
  recordStartMs = millis();
  lastSampleMs = recordStartMs;
  recordingStarted = true;
  gyroKalmanX.reset();
  gyroKalmanY.reset();
  gyroKalmanZ.reset();
  linKalmanX.reset();
  linKalmanY.reset();
  linKalmanZ.reset();

  // CSV header. Copy all serial output into BNO085_data.csv.
  Serial.println("Time_s,RawAccelX,RawAccelY,RawAccelZ,AccelX,AccelY,AccelZ,LinX,LinY,LinZ,VelX,VelY,VelZ,DispX,DispY,DispZ,GyroX_rad_s,GyroY_rad_s,GyroZ_rad_s,AngleX_rad,AngleY_rad,AngleZ_rad");
}

void setReports() {
  bno08x.enableReport(SH2_ACCELEROMETER, SENSOR_REPORT_INTERVAL_US);
  bno08x.enableReport(SH2_LINEAR_ACCELERATION, SENSOR_REPORT_INTERVAL_US);
  bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED, SENSOR_REPORT_INTERVAL_US);
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
    calibrated = false;
    gyroCalibrationSamples = 0;
    linCalibrationSamples = 0;
    gyroBiasX = 0;
    gyroBiasY = 0;
    gyroBiasZ = 0;
    linBiasX = 0;
    linBiasY = 0;
    linBiasZ = 0;
    correctedGyroX = 0;
    correctedGyroY = 0;
    correctedGyroZ = 0;
    gyroKalmanX.reset();
    gyroKalmanY.reset();
    gyroKalmanZ.reset();
    linKalmanX.reset();
    linKalmanY.reset();
    linKalmanZ.reset();
    accelFilterReady = false;
    linFilterReady = false;
    calibrationStartMs = millis();
    setReports();
  }

  if (bno08x.getSensorEvent(&sensorValue)) {
    switch (sensorValue.sensorId) {
      case SH2_ACCELEROMETER:
        rawAccelX = sensorValue.un.accelerometer.x;
        rawAccelY = sensorValue.un.accelerometer.y;
        rawAccelZ = sensorValue.un.accelerometer.z;

        if (!accelFilterReady) {
          accelX = rawAccelX;
          accelY = rawAccelY;
          accelZ = rawAccelZ;
          accelFilterReady = true;
        } else {
          accelX = lowPassFilter(accelX, rawAccelX);
          accelY = lowPassFilter(accelY, rawAccelY);
          accelZ = lowPassFilter(accelZ, rawAccelZ);
        }
        break;

      case SH2_LINEAR_ACCELERATION:
        if (!calibrated) {
          linBiasX += sensorValue.un.linearAcceleration.x;
          linBiasY += sensorValue.un.linearAcceleration.y;
          linBiasZ += sensorValue.un.linearAcceleration.z;
          linCalibrationSamples++;
          break;
        }

        {
          float correctedLinX = sensorValue.un.linearAcceleration.x - linBiasX;
          float correctedLinY = sensorValue.un.linearAcceleration.y - linBiasY;
          float correctedLinZ = sensorValue.un.linearAcceleration.z - linBiasZ;
          float gyroMagnitude = sqrtf(
            correctedGyroX * correctedGyroX +
            correctedGyroY * correctedGyroY +
            correctedGyroZ * correctedGyroZ
          );
          float linMeasurementNoise = LIN_KALMAN_MEASUREMENT_NOISE +
                                      LIN_KALMAN_GYRO_NOISE_GAIN * gyroMagnitude;

          linX = linKalmanX.update(correctedLinX, LIN_KALMAN_PROCESS_NOISE, linMeasurementNoise);
          linY = linKalmanY.update(correctedLinY, LIN_KALMAN_PROCESS_NOISE, linMeasurementNoise);
          linZ = linKalmanZ.update(correctedLinZ, LIN_KALMAN_PROCESS_NOISE, linMeasurementNoise);
          linFilterReady = true;
        }

        if (recordingStarted) {
          unsigned long nowLinMicros = micros();
          if (lastLinMicros != 0) {
            float dt = (nowLinMicros - lastLinMicros) / 1000000.0;
            float integrationLinX = removeSmallLinearDrift(linX);
            float integrationLinY = removeSmallLinearDrift(linY);
            float integrationLinZ = removeSmallLinearDrift(linZ);
            float oldVelX = velX;
            float oldVelY = velY;
            float oldVelZ = velZ;

            velX += 0.5 * (prevLinX + integrationLinX) * dt;
            velY += 0.5 * (prevLinY + integrationLinY) * dt;
            velZ += 0.5 * (prevLinZ + integrationLinZ) * dt;

            dispX += 0.5 * (oldVelX + velX) * dt;
            dispY += 0.5 * (oldVelY + velY) * dt;
            dispZ += 0.5 * (oldVelZ + velZ) * dt;

            if (isStill()) {
              stillSamples++;
            } else {
              stillSamples = 0;
            }

            if (stillSamples >= STILL_SAMPLE_LIMIT) {
              dampVelocity(velX);
              dampVelocity(velY);
              dampVelocity(velZ);
            }
          }

          prevLinX = removeSmallLinearDrift(linX);
          prevLinY = removeSmallLinearDrift(linY);
          prevLinZ = removeSmallLinearDrift(linZ);
          lastLinMicros = nowLinMicros;
        }
        break;

      case SH2_GYROSCOPE_CALIBRATED:
        gyroX = sensorValue.un.gyroscope.x;
        gyroY = sensorValue.un.gyroscope.y;
        gyroZ = sensorValue.un.gyroscope.z;

        if (!calibrated) {
          gyroBiasX += gyroX;
          gyroBiasY += gyroY;
          gyroBiasZ += gyroZ;
          gyroCalibrationSamples++;
          break;
        }

        correctedGyroX = gyroKalmanX.update(
          gyroX - gyroBiasX,
          GYRO_KALMAN_PROCESS_NOISE,
          GYRO_KALMAN_MEASUREMENT_NOISE
        );
        correctedGyroY = gyroKalmanY.update(
          gyroY - gyroBiasY,
          GYRO_KALMAN_PROCESS_NOISE,
          GYRO_KALMAN_MEASUREMENT_NOISE
        );
        correctedGyroZ = gyroKalmanZ.update(
          gyroZ - gyroBiasZ,
          GYRO_KALMAN_PROCESS_NOISE,
          GYRO_KALMAN_MEASUREMENT_NOISE
        );

        if (recordingStarted) {
          unsigned long nowGyroMicros = micros();
          if (lastGyroMicros != 0) {
            float dt = (nowGyroMicros - lastGyroMicros) / 1000000.0;

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
  if (!calibrated) {
    if (nowMs - calibrationStartMs >= CALIBRATION_MS &&
        gyroCalibrationSamples > 0 &&
        linCalibrationSamples > 0) {
      gyroBiasX /= gyroCalibrationSamples;
      gyroBiasY /= gyroCalibrationSamples;
      gyroBiasZ /= gyroCalibrationSamples;
      linBiasX /= linCalibrationSamples;
      linBiasY /= linCalibrationSamples;
      linBiasZ /= linCalibrationSamples;
      calibrated = true;
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
  Serial.print(rawAccelX, 6);
  Serial.print(',');
  Serial.print(rawAccelY, 6);
  Serial.print(',');
  Serial.print(rawAccelZ, 6);
  Serial.print(',');

  Serial.print(accelX, 6);
  Serial.print(',');
  Serial.print(accelY, 6);
  Serial.print(',');
  Serial.print(accelZ, 6);
  Serial.print(',');

  Serial.print(linX, 6);
  Serial.print(',');
  Serial.print(linY, 6);
  Serial.print(',');
  Serial.print(linZ, 6);
  Serial.print(',');

  Serial.print(velX, 6);
  Serial.print(',');
  Serial.print(velY, 6);
  Serial.print(',');
  Serial.print(velZ, 6);
  Serial.print(',');

  Serial.print(dispX, 6);
  Serial.print(',');
  Serial.print(dispY, 6);
  Serial.print(',');
  Serial.print(dispZ, 6);
  Serial.print(',');

  Serial.print(correctedGyroX, 6);
  Serial.print(',');
  Serial.print(correctedGyroY, 6);
  Serial.print(',');
  Serial.print(correctedGyroZ, 6);
  Serial.print(',');

  Serial.print(angleX, 6);
  Serial.print(',');
  Serial.print(angleY, 6);
  Serial.print(',');
  Serial.println(angleZ, 6);
}
