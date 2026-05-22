#include "imu_calibration.h"

using namespace Imu;

Imu::Vector3f Imu::ImuCalibration::applyAccel(
    const Vector3f& raw, const AccelCalibration& calibration) {
  Vector3f out;
  out.x = (raw.x - calibration.bias.x) / calibration.scale.x;
  out.y = (raw.y - calibration.bias.y) / calibration.scale.y;
  out.z = (raw.z - calibration.bias.z) / calibration.scale.z;
  return out;
}

bool Imu::ImuCalibration::saveAccelCalibration(
    JsonDocument& doc, const AccelCalibration& calibration) {
  doc["bias"]["x"] = calibration.bias.x;
  doc["bias"]["y"] = calibration.bias.y;
  doc["bias"]["z"] = calibration.bias.z;
  doc["scale"]["x"] = calibration.scale.x;
  doc["scale"]["y"] = calibration.scale.y;
  doc["scale"]["z"] = calibration.scale.z;
  doc["valid"] = calibration.valid;
  return true;
}

bool Imu::ImuCalibration::loadAccelCalibration(const JsonDocument& doc,
                                               AccelCalibration& calibration) {
  if (!doc.containsKey("bias") || !doc.containsKey("scale")) return false;
  calibration.bias.x = doc["bias"]["x"] | 0.0f;
  calibration.bias.y = doc["bias"]["y"] | 0.0f;
  calibration.bias.z = doc["bias"]["z"] | 0.0f;
  calibration.scale.x = doc["scale"]["x"] | 1.0f;
  calibration.scale.y = doc["scale"]["y"] | 1.0f;
  calibration.scale.z = doc["scale"]["z"] | 1.0f;
  calibration.valid = doc["valid"] | false;
  return true;
}

bool Imu::ImuCalibration::fitAccelCalibrationFromPoseMeans(
    const AccelPoseMean& xPos, const AccelPoseMean& xNeg,
    const AccelPoseMean& yPos, const AccelPoseMean& yNeg,
    const AccelPoseMean& zPos, const AccelPoseMean& zNeg,
    AccelCalibration& calibration, float gravity) {
  // Basic diagonal fit from pose means
  calibration.bias.x = 0.5f * (xPos.mean.x + xNeg.mean.x);
  calibration.bias.y = 0.5f * (yPos.mean.y + yNeg.mean.y);
  calibration.bias.z = 0.5f * (zPos.mean.z + zNeg.mean.z);

  calibration.scale.x = (xPos.mean.x - xNeg.mean.x) / (2.0f * gravity);
  calibration.scale.y = (yPos.mean.y - yNeg.mean.y) / (2.0f * gravity);
  calibration.scale.z = (zPos.mean.z - zNeg.mean.z) / (2.0f * gravity);

  calibration.valid = true;
  return true;
}
