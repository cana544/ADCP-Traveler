#include <Arduino.h>

#include "imu_calibration.h"

using namespace Imu;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("IMU calibration test");

  AccelPoseMean xp{{10.37f, -0.10f, -0.15f}, 1000};
  AccelPoseMean xn{{-9.03f, -0.30f, -0.31f}, 1000};
  AccelPoseMean yp{{0.44f, 9.60f, -0.49f}, 1000};
  AccelPoseMean yn{{0.86f, -9.92f, -0.09f}, 1000};
  AccelPoseMean zp{{0.60f, -0.00f, 9.44f}, 1000};
  AccelPoseMean zn{{0.73f, -0.06f, -10.13f}, 1000};

  AccelCalibration cal{};
  ImuCalibration::fitAccelCalibrationFromPoseMeans(xp, xn, yp, yn, zp, zn, cal);

  Serial.print("Bias: ");
  Serial.print(cal.bias.x);
  Serial.print(",");
  Serial.print(cal.bias.y);
  Serial.print(",");
  Serial.println(cal.bias.z);

  Serial.print("Scale: ");
  Serial.print(cal.scale.x);
  Serial.print(",");
  Serial.print(cal.scale.y);
  Serial.print(",");
  Serial.println(cal.scale.z);
}

void loop() {}
