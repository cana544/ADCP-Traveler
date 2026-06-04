#ifndef IMU_CALIBRATION_H
#define IMU_CALIBRATION_H

#include <Arduino.h>
#include <ArduinoJson.h>

namespace Imu {

struct Vector3f {
  float x;
  float y;
  float z;
};

struct AccelPoseMean {
  Vector3f mean;
  size_t count;
};

struct AccelCalibration {
  Vector3f bias;
  Vector3f scale;
  bool valid;
};

struct AccelFullCalibration {
  float M[3][3];
  Vector3f b;
  bool valid;
};

class ImuCalibration {
 public:
  static Vector3f applyAccel(const Vector3f& raw,
                             const AccelCalibration& calibration);
  static bool saveAccelCalibration(JsonDocument& doc,
                                   const AccelCalibration& calibration);
  static bool loadAccelCalibration(const JsonDocument& doc,
                                   AccelCalibration& calibration);
  static Vector3f applyAccelFull(const Vector3f& raw,
                                 const AccelFullCalibration& calibration);
  static bool saveAccelFullCalibration(JsonDocument& doc,
                                       const AccelFullCalibration& calibration);
  static bool loadAccelFullCalibration(const JsonDocument& doc,
                                       AccelFullCalibration& calibration);
  static bool fitAccelCalibrationFromPoseMeans(
      const AccelPoseMean& xPos, const AccelPoseMean& xNeg,
      const AccelPoseMean& yPos, const AccelPoseMean& yNeg,
      const AccelPoseMean& zPos, const AccelPoseMean& zNeg,
      AccelCalibration& calibration, float gravity = 9.80665f);
};

}  // namespace Imu

#endif
