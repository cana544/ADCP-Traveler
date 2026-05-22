/* ICM-20948 version of the BNO085 capture sketch.
   Requires the SparkFun ICM-20948 Arduino Library:
   https://github.com/sparkfun/SparkFun_ICM-20948_ArduinoLibrary
*/
#include <ICM_20948.h>
#include <Wire.h>
#include <math.h>

// ESP32 I2C pins. These keep the same physical GPIOs used by the BNO085 test.
#define ICM_SDA_PIN 16
#define ICM_SCL_PIN 17

// Match this to the ICM-20948 AD0/SDO pin:
// 0 = address 0x68, 1 = address 0x69.
#define ICM_AD0_VAL 1

ICM_20948_I2C icm;

const unsigned long SAMPLE_PERIOD_MS = 20;
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
const float MG_TO_MS2 = 9.80665f / 1000.0f;
const float DPS_TO_RADPS = 0.017453292519943295f;

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
float gravityBiasX = 0, gravityBiasY = 0, gravityBiasZ = 0;
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

  // CSV header. Same column order as the BNO085 full-output sketch.
  Serial.println(
      "Time_s,RawAccelX,RawAccelY,RawAccelZ,MagX,MagY,MagZ,AccelX,AccelY,"
      "AccelZ,LinX,LinY,"
      "LinZ,VelX,VelY,VelZ,DispX,DispY,DispZ,GyroX_rad_s,GyroY_rad_s,GyroZ_rad_"
      "s,AngleX_rad,AngleY_rad,AngleZ_rad");
}

void resetCaptureState() {
  recordingStarted = false;
  recordingFinished = false;
  calibrated = false;
  gyroCalibrationSamples = 0;
  linCalibrationSamples = 0;
  gyroBiasX = 0;
  gyroBiasY = 0;
  gyroBiasZ = 0;
  gravityBiasX = 0;
  gravityBiasY = 0;
  gravityBiasZ = 0;
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
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("BOOT: sketch setup start");

  Wire.begin(ICM_SDA_PIN, ICM_SCL_PIN);
  Wire.setClock(400000);
  Serial.println("BOOT: I2C started");

  Serial.println("BOOT: waiting for ICM20948 over I2C");
  icm.begin(Wire, ICM_AD0_VAL);
  if (icm.status != ICM_20948_Stat_Ok) {
    Serial.print("Failed to find ICM20948 over I2C: ");
    Serial.println(icm.statusString());
    while (1) {
      delay(10);
    }
  }

  Serial.println("BOOT: ICM20948 found");
  Serial.println("BOOT: reports enabled");

  calibrationStartMs = millis();
  Serial.println("BOOT: calibration timer started");
}

// ICM-specific accel calibration. Identity by default because the BNO085
// calibration matrix should not be reused on a different IMU.
const float CAL_M[3][3] = {{1.0f, 0.0f, 0.0f},
                           {0.0f, 1.0f, 0.0f},
                           {0.0f, 0.0f, 1.0f}};
const float CAL_b[3] = {0.0f, 0.0f, 0.0f};

void readIcmSample() {
  icm.getAGMT();

  rawAccelUncalX = icm.accX() * MG_TO_MS2;
  rawAccelUncalY = icm.accY() * MG_TO_MS2;
  rawAccelUncalZ = icm.accZ() * MG_TO_MS2;

  float mx = rawAccelUncalX;
  float my = rawAccelUncalY;
  float mz = rawAccelUncalZ;

  rawAccelX =
      CAL_M[0][0] * mx + CAL_M[0][1] * my + CAL_M[0][2] * mz + CAL_b[0];
  rawAccelY =
      CAL_M[1][0] * mx + CAL_M[1][1] * my + CAL_M[1][2] * mz + CAL_b[1];
  rawAccelZ =
      CAL_M[2][0] * mx + CAL_M[2][1] * my + CAL_M[2][2] * mz + CAL_b[2];

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

  rawMagX = icm.magX();
  rawMagY = icm.magY();
  rawMagZ = icm.magZ();

  gyroX = icm.gyrX() * DPS_TO_RADPS;
  gyroY = icm.gyrY() * DPS_TO_RADPS;
  gyroZ = icm.gyrZ() * DPS_TO_RADPS;

  if (!calibrated) {
    gyroBiasX += gyroX;
    gyroBiasY += gyroY;
    gyroBiasZ += gyroZ;
    gyroCalibrationSamples++;

    gravityBiasX += rawAccelX;
    gravityBiasY += rawAccelY;
    gravityBiasZ += rawAccelZ;
    linCalibrationSamples++;
    return;
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

  correctedGyroX = removeSmallGyroDrift(correctedGyroX);
  correctedGyroY = removeSmallGyroDrift(correctedGyroY);
  correctedGyroZ = removeSmallGyroDrift(correctedGyroZ);

  float correctedLinX = rawAccelX - gravityBiasX;
  float correctedLinY = rawAccelY - gravityBiasY;
  float correctedLinZ = rawAccelZ - gravityBiasZ;
  float gyroMagnitude = sqrtf(correctedGyroX * correctedGyroX +
                              correctedGyroY * correctedGyroY +
                              correctedGyroZ * correctedGyroZ);
  float linMeasurementNoise =
      LIN_KALMAN_MEASUREMENT_NOISE + LIN_KALMAN_GYRO_NOISE_GAIN * gyroMagnitude;

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

  if (recordingStarted) {
    unsigned long nowGyroMicros = micros();
    if (lastGyroMicros != 0) {
      float dt = (nowGyroMicros - lastGyroMicros) / 1000000.0;

      angleX += correctedGyroX * dt;
      angleY += correctedGyroY * dt;
      angleZ += correctedGyroZ * dt;
    }
    lastGyroMicros = nowGyroMicros;

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

      if (AUTO_ZERO_VELOCITY_WHEN_STILL && stillSamples >= STILL_SAMPLE_LIMIT) {
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
}

void loop() {
  if (recordingFinished) {
    return;
  }

  if (icm.dataReady()) {
    readIcmSample();
  }

  unsigned long nowMs = millis();
  if (!calibrated) {
    if (nowMs - calibrationStartMs >= CALIBRATION_MS &&
        gyroCalibrationSamples > 0 && linCalibrationSamples > 0) {
      gyroBiasX /= gyroCalibrationSamples;
      gyroBiasY /= gyroCalibrationSamples;
      gyroBiasZ /= gyroCalibrationSamples;
      gravityBiasX /= linCalibrationSamples;
      gravityBiasY /= linCalibrationSamples;
      gravityBiasZ /= linCalibrationSamples;
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

  Serial.print(rawAccelUncalX, 6);
  Serial.print(',');
  Serial.print(rawAccelUncalY, 6);
  Serial.print(',');
  Serial.print(rawAccelUncalZ, 6);
  Serial.print(',');

  Serial.print(rawMagX, 6);
  Serial.print(',');
  Serial.print(rawMagY, 6);
  Serial.print(',');
  Serial.print(rawMagZ, 6);
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
