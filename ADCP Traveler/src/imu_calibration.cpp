#include "imu_calibration.h"

using namespace Imu;

namespace {

bool parseVector3(const JsonVariantConst& value, Imu::Vector3f& out) {
  if (value.is<JsonArrayConst>()) {
    JsonArrayConst arr = value.as<JsonArrayConst>();
    if (arr.size() < 3) return false;
    out.x = arr[0] | 0.0f;
    out.y = arr[1] | 0.0f;
    out.z = arr[2] | 0.0f;
    return true;
  }

  if (value.is<JsonObjectConst>()) {
    JsonObjectConst obj = value.as<JsonObjectConst>();
    out.x = obj["x"] | 0.0f;
    out.y = obj["y"] | 0.0f;
    out.z = obj["z"] | 0.0f;
    return true;
  }

  return false;
}

}  // namespace

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

  if (!parseVector3(doc["bias"], calibration.bias)) return false;
  if (!parseVector3(doc["scale"], calibration.scale)) return false;

  calibration.valid = doc["valid"] | false;
  return true;
}

Imu::Vector3f Imu::ImuCalibration::applyAccelFull(
    const Vector3f& raw, const AccelFullCalibration& calibration) {
  Vector3f out;
  out.x = calibration.M[0][0] * raw.x + calibration.M[0][1] * raw.y +
          calibration.M[0][2] * raw.z + calibration.b.x;
  out.y = calibration.M[1][0] * raw.x + calibration.M[1][1] * raw.y +
          calibration.M[1][2] * raw.z + calibration.b.y;
  out.z = calibration.M[2][0] * raw.x + calibration.M[2][1] * raw.y +
          calibration.M[2][2] * raw.z + calibration.b.z;
  return out;
}

bool Imu::ImuCalibration::saveAccelFullCalibration(
    JsonDocument& doc, const AccelFullCalibration& calibration) {
  JsonArray matrix = doc["M"].to<JsonArray>();
  for (int row = 0; row < 3; ++row) {
    JsonArray matrixRow = matrix.createNestedArray();
    for (int col = 0; col < 3; ++col) {
      matrixRow.add(calibration.M[row][col]);
    }
  }

  JsonArray b = doc["b"].to<JsonArray>();
  b.add(calibration.b.x);
  b.add(calibration.b.y);
  b.add(calibration.b.z);

  doc["valid"] = calibration.valid;
  doc["method"] = "least_squares_3x3";
  return true;
}

bool Imu::ImuCalibration::loadAccelFullCalibration(
    const JsonDocument& doc, AccelFullCalibration& calibration) {
  if (!doc.containsKey("M") || !doc.containsKey("b")) return false;

  JsonArrayConst matrix = doc["M"].as<JsonArrayConst>();
  if (matrix.size() < 3) return false;

  for (int row = 0; row < 3; ++row) {
    JsonArrayConst matrixRow = matrix[row].as<JsonArrayConst>();
    if (matrixRow.size() < 3) return false;
    for (int col = 0; col < 3; ++col) {
      calibration.M[row][col] = matrixRow[col] | 0.0f;
    }
  }

  if (!parseVector3(doc["b"], calibration.b)) return false;

  calibration.valid = doc["valid"] | true;
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
