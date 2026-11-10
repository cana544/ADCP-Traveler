#pragma once
#include <cstdint>
#include <cmath>

struct ImuData {
  enum Report { ACCEL, LINEAR, GYRO, ORIENTATION, COUNT };
  float values[COUNT][4] = {};
  uint32_t receivedUs[COUNT] = {}, resets = 0, errors = 0;
  uint64_t sensorUs[COUNT] = {};
  uint8_t accuracy[COUNT] = {};
  bool valid[COUNT] = {}, qualityReady = false, qualityPass = false;
  float qualityMeanNorm = 0, qualityM2 = 0, qualityMeanGyro = 0;
  uint16_t qualitySamples = 0;
  bool checking = false;
  uint32_t age(Report report, uint32_t now) const { return valid[report] ? now - receivedUs[report] : UINT32_MAX; }
  bool healthy(uint32_t now) const {
    for (int i = 0; i < COUNT; ++i) if (!valid[i] || now - receivedUs[i] >= 500000UL) return false;
    return true;
  }
  void reset() {
    ++resets; for (bool& item : valid) item = false;
    checking = qualityReady = qualityPass = false;
  }
  void startQualityCheck() {
    checking = true; qualityReady = qualityPass = false;
    qualitySamples = 0; qualityMeanNorm = qualityM2 = qualityMeanGyro = 0;
  }
  void vector(Report report, float x, float y, float z, uint32_t host,
              uint64_t sensor, uint8_t status) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
      valid[report] = false; ++errors; return;
    }
    values[report][0] = x; values[report][1] = y; values[report][2] = z;
    valid[report] = true; receivedUs[report] = host; sensorUs[report] = sensor; accuracy[report] = status & 3;
    if (checking && report == ACCEL) {
      if (age(GYRO, host) > 100000UL) { checking = false; qualityReady = true; qualityPass = false; return; }
      const float norm = std::sqrt(x*x + y*y + z*z);
      const float* g = values[GYRO];
      const float gyroNorm = std::sqrt(g[0]*g[0] + g[1]*g[1] + g[2]*g[2]);
      ++qualitySamples;
      const float delta = norm - qualityMeanNorm;
      qualityMeanNorm += delta / qualitySamples; qualityM2 += delta * (norm - qualityMeanNorm);
      qualityMeanGyro += (gyroNorm - qualityMeanGyro) / qualitySamples;
      if (qualitySamples >= 150) {
        checking = false; qualityReady = true;
        qualityPass = std::fabs(qualityMeanNorm - 9.80665f) < .2f &&
            std::sqrt(qualityM2 / (qualitySamples - 1)) < .2f && qualityMeanGyro < .03f;
      }
    }
  }
  void quaternion(float x, float y, float z, float w, uint32_t host, uint64_t sensor, uint8_t status) {
    const float norm = x*x + y*y + z*z + w*w;
    if (!std::isfinite(norm) || std::fabs(norm - 1) > .1f) { valid[ORIENTATION] = false; ++errors; return; }
    vector(ORIENTATION, x, y, z, host, sensor, status); values[ORIENTATION][3] = w;
  }
};
