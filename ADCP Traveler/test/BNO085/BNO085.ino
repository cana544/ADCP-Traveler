/* Copied from original BNO085.ino for organization */
#include <Adafruit_BNO08x.h>
#include <math.h>

// ESP32 UART2 pins
#define BNO_RX_PIN 16  // ESP32 RX2, connect to BNO085 SDA
#define BNO_TX_PIN 17  // ESP32 TX2, connect to BNO085 SCL

#define BNO08X_RESET -1

Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;

const unsigned long SAMPLE_PERIOD_MS = 20;
const uint32_t SENSOR_REPORT_INTERVAL_US = 5000;
const unsigned long RECORD_DURATION_MS = 30000;
const unsigned long CALIBRATION_MS = 5000;
const float GATE_TARGET_NORM_MS2 = 9.80665f;
const float GATE_MEAN_NORM_TOL_MS2 = 0.20f;
const float GATE_STD_NORM_MAX_MS2 = 0.20f;
const float GATE_MEAN_GYRO_MAX_RADPS = 0.03f;
const unsigned long GATE_MIN_SAMPLES = 800;
const float ACCEL_FILTER_ALPHA = 0.1;
const float GYRO_DEADBAND_RADPS = 0.003;
const float LINEAR_ACCEL_DEADBAND_MS2 = 0.015;
const float STILL_ACCEL_THRESHOLD_MS2 = 0.04;
const float STILL_GYRO_THRESHOLD_RADPS = 0.02;
const float VELOCITY_ZERO_THRESHOLD_MS = 0.002;
const float VELOCITY_DECAY_WHEN_STILL = 0.5;
const unsigned int STILL_SAMPLE_LIMIT = 8;
const bool AUTO_ZERO_VELOCITY_WHEN_STILL = false;
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
float rawAccelUncalX = 0, rawAccelUncalY = 0, rawAccelUncalZ = 0;
float accelX = 0, accelY = 0, accelZ = 0;
float rawMagX = 0, rawMagY = 0, rawMagZ = 0;
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
unsigned long gateSampleCount = 0;
float gateSumNorm = 0.0f;
float gateSumNormSq = 0.0f;
float gateSumGyroNorm = 0.0f;

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
  if (accelValue > -LINEAR_ACCEL_DEADBAND_MS2 &&
      accelValue < LINEAR_ACCEL_DEADBAND_MS2) {
    return 0.0;
  }
  return accelValue;
}

void dampVelocity(float& velocity) {
  velocity *= VELOCITY_DECAY_WHEN_STILL;
  if (velocity > -VELOCITY_ZERO_THRESHOLD_MS &&
      velocity < VELOCITY_ZERO_THRESHOLD_MS) {
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

void printCaptureGateResult() {
  if (gateSampleCount == 0) {
    Serial.println("GATE,FAIL");
    Serial.println("GATE_REASON,no_samples");
    return;
  }

  float invN = 1.0f / static_cast<float>(gateSampleCount);
  float meanNorm = gateSumNorm * invN;
  float meanNormSq = gateSumNormSq * invN;
  float varNorm = meanNormSq - (meanNorm * meanNorm);
  if (varNorm < 0.0f) {
    varNorm = 0.0f;
  }
  float stdNorm = sqrtf(varNorm);
  float meanGyroNorm = gateSumGyroNorm * invN;

  bool pass = true;
  if (gateSampleCount < GATE_MIN_SAMPLES) {
    pass = false;
  }
  if (fabsf(meanNorm - GATE_TARGET_NORM_MS2) > GATE_MEAN_NORM_TOL_MS2) {
    pass = false;
  }
  if (stdNorm > GATE_STD_NORM_MAX_MS2) {
    pass = false;
  }
  if (meanGyroNorm > GATE_MEAN_GYRO_MAX_RADPS) {
    pass = false;
  }

  Serial.print("GATE,");
  Serial.println(pass ? "PASS" : "FAIL");

  Serial.print("GATE_METRICS,samples=");
  Serial.print(gateSampleCount);
  Serial.print(",mean_norm=");
  Serial.print(meanNorm, 6);
  Serial.print(",std_norm=");
  Serial.print(stdNorm, 6);
  Serial.print(",mean_gyro_norm=");
  Serial.println(meanGyroNorm, 6);

  if (gateSampleCount < GATE_MIN_SAMPLES) {
    Serial.println("GATE_REASON,too_few_samples");
  }
  if (fabsf(meanNorm - GATE_TARGET_NORM_MS2) > GATE_MEAN_NORM_TOL_MS2) {
    Serial.println("GATE_REASON,bad_mean_gravity_norm");
  }
  if (stdNorm > GATE_STD_NORM_MAX_MS2) {
    Serial.println("GATE_REASON,too_much_motion_or_noise");
  }
  if (meanGyroNorm > GATE_MEAN_GYRO_MAX_RADPS) {
    Serial.println("GATE_REASON,gyro_not_still_enough");
  }
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
  gateSampleCount = 0;
  gateSumNorm = 0.0f;
  gateSumNormSq = 0.0f;
  gateSumGyroNorm = 0.0f;

  // CSV header. Copy all serial output into BNO085_data.csv.
#ifdef SH2_MAGNETIC_FIELD
  Serial.println(
      "Time_s,RawAccelX,RawAccelY,RawAccelZ,MagX,MagY,MagZ,AccelX,AccelY,"
      "AccelZ,LinX,LinY,"
      "LinZ,VelX,VelY,VelZ,DispX,DispY,DispZ,GyroX_rad_s,GyroY_rad_s,GyroZ_rad_"
      "s,AngleX_rad,AngleY_rad,AngleZ_rad");
#else
  Serial.println(
      "Time_s,RawAccelX,RawAccelY,RawAccelZ,AccelX,AccelY,"
      "AccelZ,LinX,LinY,"
      "LinZ,VelX,VelY,VelZ,DispX,DispY,DispZ,GyroX_rad_s,GyroY_rad_s,GyroZ_rad_"
      "s,AngleX_rad,AngleY_rad,AngleZ_rad");
#endif
}

void setReports() {
  bno08x.enableReport(SH2_ACCELEROMETER, SENSOR_REPORT_INTERVAL_US);
  bno08x.enableReport(SH2_LINEAR_ACCELERATION, SENSOR_REPORT_INTERVAL_US);
#ifdef SH2_MAGNETIC_FIELD
  bno08x.enableReport(SH2_MAGNETIC_FIELD, SENSOR_REPORT_INTERVAL_US);
#endif
  bno08x.enableReport(SH2_GYROSCOPE_CALIBRATED, SENSOR_REPORT_INTERVAL_US);
  bno08x.enableReport(SH2_ROTATION_VECTOR, SENSOR_REPORT_INTERVAL_US);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("BOOT: sketch setup start");

  // UART2 for BNO085
  Serial2.begin(115200, SERIAL_8N1, BNO_RX_PIN, BNO_TX_PIN);
  Serial.println("BOOT: Serial2 started");

  Serial.println("BOOT: waiting for BNO085 over UART");
  if (!bno08x.begin_UART(&Serial2)) {
    Serial.println("Failed to find BNO085 over UART");
    while (1) {
      delay(10);
    }
  }

  Serial.println("BOOT: BNO085 found");

  setReports();
  Serial.println("BOOT: reports enabled");

  calibrationStartMs = millis();
  Serial.println("BOOT: calibration timer started");
}

// --- Calibration matrix (M) and bias (b) from accel_full_fit.py ---
// Model: a_true = M * a_meas + b
const float CAL_M[3][3] = {{1.005674f, 0.013484f, 0.005187f},
                           {0.005849f, 1.003525f, 0.002901f},
                           {0.000987f, 0.021993f, 1.007129f}};
const float CAL_b[3] = {0.33860107f, 0.22661088f, 0.34864652f};

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
    gateSampleCount = 0;
    gateSumNorm = 0.0f;
    gateSumNormSq = 0.0f;
    gateSumGyroNorm = 0.0f;
    accelFilterReady = false;
    linFilterReady = false;
    calibrationStartMs = millis();
    setReports();
  }

  if (bno08x.getSensorEvent(&sensorValue)) {
    switch (sensorValue.sensorId) {
      case SH2_ACCELEROMETER:
        // Preserve uncalibrated readings for logging
        rawAccelUncalX = sensorValue.un.accelerometer.x;
        rawAccelUncalY = sensorValue.un.accelerometer.y;
        rawAccelUncalZ = sensorValue.un.accelerometer.z;

        // Apply full 3x3 calibration: a_true = M * a_meas + b
        {
          float mx = rawAccelUncalX;
          float my = rawAccelUncalY;
          float mz = rawAccelUncalZ;

          rawAccelX =
              CAL_M[0][0] * mx + CAL_M[0][1] * my + CAL_M[0][2] * mz + CAL_b[0];
          rawAccelY =
              CAL_M[1][0] * mx + CAL_M[1][1] * my + CAL_M[1][2] * mz + CAL_b[1];
          rawAccelZ =
              CAL_M[2][0] * mx + CAL_M[2][1] * my + CAL_M[2][2] * mz + CAL_b[2];
        }

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

#ifdef SH2_MAGNETIC_FIELD
      case SH2_MAGNETIC_FIELD:
        // Raw magnetometer reading (microtesla or sensor units depending on
        // report)
        rawMagX = sensorValue.un.magneticField.x;
        rawMagY = sensorValue.un.magneticField.y;
        rawMagZ = sensorValue.un.magneticField.z;
        break;
#endif

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
          float gyroMagnitude = sqrtf(correctedGyroX * correctedGyroX +
                                      correctedGyroY * correctedGyroY +
                                      correctedGyroZ * correctedGyroZ);
          float linMeasurementNoise =
              LIN_KALMAN_MEASUREMENT_NOISE +
              LIN_KALMAN_GYRO_NOISE_GAIN * gyroMagnitude;

          linX = linKalmanX.update(correctedLinX, LIN_KALMAN_PROCESS_NOISE,
                                   linMeasurementNoise);
          linY = linKalmanY.update(correctedLinY, LIN_KALMAN_PROCESS_NOISE,
                                   linMeasurementNoise);
          linZ = linKalmanZ.update(correctedLinZ, LIN_KALMAN_PROCESS_NOISE,
                                   linMeasurementNoise);
          linX = removeSmallLinearDrift(linX);
          linY = removeSmallLinearDrift(linY);
          linZ = removeSmallLinearDrift(linZ);
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

            if (AUTO_ZERO_VELOCITY_WHEN_STILL &&
                stillSamples >= STILL_SAMPLE_LIMIT) {
              velX = 0;
              velY = 0;
              velZ = 0;
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

        correctedGyroX =
            gyroKalmanX.update(gyroX - gyroBiasX, GYRO_KALMAN_PROCESS_NOISE,
                               GYRO_KALMAN_MEASUREMENT_NOISE);
        correctedGyroY =
            gyroKalmanY.update(gyroY - gyroBiasY, GYRO_KALMAN_PROCESS_NOISE,
                               GYRO_KALMAN_MEASUREMENT_NOISE);
        correctedGyroZ =
            gyroKalmanZ.update(gyroZ - gyroBiasZ, GYRO_KALMAN_PROCESS_NOISE,
                               GYRO_KALMAN_MEASUREMENT_NOISE);

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

      case SH2_ROTATION_VECTOR: {
        // Rotation vector (quaternion) provided by the BNO08x
        // Expect quaternion in sensorValue.un.rotationVector.real[0..3]
        float q0 = sensorValue.un.rotationVector.real;
        float q1 = sensorValue.un.rotationVector.i;
        float q2 = sensorValue.un.rotationVector.j;
        float q3 = sensorValue.un.rotationVector.k;

        // Convert quaternion to Euler angles (radians)
        // pitch
        float t = 2.0 * (q0 * q2 - q3 * q1);
        if (t > 1.0) t = 1.0;
        if (t < -1.0) t = -1.0;
        angleX = asinf(t);

        // roll
        angleY =
            atan2f(2.0 * (q0 * q1 + q2 * q3), 1.0 - 2.0 * (q1 * q1 + q2 * q2));

        // yaw
        angleZ =
            atan2f(2.0 * (q0 * q3 + q1 * q2), 1.0 - 2.0 * (q2 * q2 + q3 * q3));
      } break;
    }
  }

  unsigned long nowMs = millis();
  if (!calibrated) {
    if (nowMs - calibrationStartMs >= CALIBRATION_MS &&
        gyroCalibrationSamples > 0 && linCalibrationSamples > 0) {
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
    printCaptureGateResult();
    recordingFinished = true;
    Serial.println("END");
    return;
  }

  if (nowMs - lastSampleMs < SAMPLE_PERIOD_MS) {
    return;
  }

  lastSampleMs += SAMPLE_PERIOD_MS;

  float accelNorm = sqrtf(accelX * accelX + accelY * accelY + accelZ * accelZ);
  float gyroNorm =
      sqrtf(correctedGyroX * correctedGyroX + correctedGyroY * correctedGyroY +
            correctedGyroZ * correctedGyroZ);
  gateSampleCount++;
  gateSumNorm += accelNorm;
  gateSumNormSq += accelNorm * accelNorm;
  gateSumGyroNorm += gyroNorm;

  Serial.print((nowMs - recordStartMs) / 1000.0, 3);
  Serial.print(',');
  // Print original uncalibrated raw readings in the RawAccel columns
  Serial.print(rawAccelUncalX, 6);
  Serial.print(',');
  Serial.print(rawAccelUncalY, 6);
  Serial.print(',');
  Serial.print(rawAccelUncalZ, 6);

#ifdef SH2_MAGNETIC_FIELD
  Serial.print(',');
  Serial.print(rawMagX, 6);
  Serial.print(',');
  Serial.print(rawMagY, 6);
  Serial.print(',');
  Serial.print(rawMagZ, 6);
#endif

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
  Serial.print(angleZ, 6);
  Serial.println();
}
