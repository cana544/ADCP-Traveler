# BNO085 IMU Calibration TODO (Paper-Aligned)

This checklist mirrors the paper workflow and maps each step to what you need for BNO085.

Reference paper (in this repo): `test/BNO085/docs/Design_and_Implementation_of_Motion_Tracking_System_Based_on_IMU.pdf`

## 0) Data Capture Setup

- [x] Firmware logger created (`test/BNO085/BNO085.ino`)
- [x] CSV capture enabled with raw accel columns (`RawAccelX`, `RawAccelY`, `RawAccelZ`)
- [x] 30 s gate + quality metrics added (`GATE_METRICS`, `GATE_REASON`)
- [x] Confirm capture rig is rigid enough (low vibration, no slipping)

What this includes:

- Keep IMU still for each static pose.
- Record enough samples (>= 1000 preferred).
- Reject windows with high std or clear transients.

## 1) Accelerometer Calibration (6-sided + full model)

Paper intent:

- Use six static orientations (+X, -X, +Y, -Y, +Z, -Z).
- Estimate bias and scale/misalignment.

### 1.1 Gather six-pose data

- [x] Per-pose files collected (`X+.csv`, `X-.csv`, `Y+.csv`, `Y-.csv`, `Z+.csv`, `Z-.csv`)
- [x] Merged labeled dataset created (`accel_static_poses.csv`)
- [ ] Re-capture bad poses until gate passes cleanly (especially high-std poses)

How it is done now:

- Merge script: `test/BNO085/scripts/merge_static_pose_csvs.py`
- Output format: `Pose,Time_s,RawAccelX,RawAccelY,RawAccelZ`

### 1.2 Fit simple 6-face bias/scale

- [x] Implemented in `test/BNO085/scripts/accel_calibrate.py`
- [x] Uses pose means (`pose_means_6_face`) when labels exist
- [x] Writes `accel_calibration.json`

How values are computed:

- Bias per axis:
  - `bx = 0.5 * (mean(X+) + mean(X-))`
  - `by = 0.5 * (mean(Y+) + mean(Y-))`
  - `bz = 0.5 * (mean(Z+) + mean(Z-))`
- Scale per axis:
  - `sx = (mean(X+) - mean(X-)) / (2*g)`
  - `sy = (mean(Y+) - mean(Y-)) / (2*g)`
  - `sz = (mean(Z+) - mean(Z-)) / (2*g)`

### 1.3 Fit full matrix model (recommended)

- [x] Implemented in `test/BNO085/scripts/accel_full_fit.py`
- [x] Solves least squares for full matrix + bias
- [x] Writes `accel_full_calibration.json` with `M`, `b`, RMSE
- [ ] Refit after clean re-capture to reduce RMSE and std spread

Model used:

- `a_true = M * a_meas + b`

How to get M and b:

1. Capture six labeled static poses.
2. Run `accel_full_fit.py` on `accel_static_poses.csv`.
3. Copy output `M` and `b` into firmware constants (`CAL_M`, `CAL_b`) or future persistent storage.

## 2) Magnetometer Calibration (ellipsoid fit)

Paper intent:

- Collect many magnetometer samples in many orientations.
- Fit ellipsoid to remove hard/soft iron distortion.

- [x] Magnetometer fields added to logger (`MagX`, `MagY`, `MagZ`)
- [ ] Create magnetometer collection routine (target ~2500+ varied orientation samples)
- [ ] Implement ellipsoid fitting script
- [ ] Export mag calibration params and apply in firmware

What this includes:

- Slow 3D rotations covering full orientation space.
- Remove offsets (center) and scale/misalignment (shape).

## 3) Gyroscope Calibration (bias and drift)

Paper intent:

- Estimate gyro bias from still data and compensate drift.

- [ ] Capture still gyro windows at startup (and optionally periodic re-check)
- [ ] Compute per-axis gyro bias (mean at rest)
- [ ] Subtract bias from gyro stream before integration/fusion
- [ ] Add temperature-dependent bias table or linear model if needed

How to get bias values:

- With IMU fully still, average `GyroX/Y/Z` over a clean window.
- Use these means as `gyroBiasX/Y/Z`.

## 4) Attitude Fusion (AEKF)

Paper intent:

- Use gyro for prediction, accel+mag for correction.
- Adapt noise parameters for better robustness.

- [ ] Add `imu_fusion` module for quaternion/state update
- [ ] Implement predict-correct loop (gyro predict, accel/mag correct)
- [ ] Add adaptive covariance tuning (AEKF behavior)
- [ ] Compare against existing simple filter behavior

## 5) Motion Tracking Integration

- [ ] Use fused orientation for gravity compensation
- [ ] Integrate linear acceleration -> velocity -> position
- [ ] Add ZUPT or motion gating to limit drift
- [ ] Validate with known motion tests

## 6) Persistence and Deployment

- [ ] Save calibration constants (`M`, `b`, gyro bias, mag params) to persistent storage
- [ ] Load constants at boot and validate checksum/version
- [ ] Add command to print current calibration package

## 7) Validation and Acceptance Criteria

- [x] Capture quality report script/result available (`accel_capture_report.json`)
- [ ] Accelerometer per-pose norm close to g with low std on all six faces
- [ ] Gyro bias near zero after compensation in still test
- [ ] Mag data approximately spherical after ellipsoid correction
- [ ] Fused attitude stable at rest and responsive in motion

Suggested acceptance thresholds (can tune):

- Static accel norm mean error: <= 0.05 to 0.10 m/s^2
- Static accel norm std: <= 0.10 to 0.15 m/s^2
- Mean gyro norm at rest: very small and stable

## Quick Run Commands

```bash
python test/BNO085/scripts/merge_static_pose_csvs.py
python test/BNO085/scripts/accel_calibrate.py test/BNO085/data/accel_static_poses.csv
python test/BNO085/scripts/accel_full_fit.py test/BNO085/data/accel_static_poses.csv
```

## Current Status Summary

- Data gathering for accelerometer six poses: DONE, but quality mixed.
- Method to compute `M` and `b`: DONE (`accel_full_fit.py`).
- Firmware application path (`CAL_M`, `CAL_b`): DONE in test sketch.
- Remaining major blockers: clean pose captures, magnetometer ellipsoid fit, gyro bias/temp model, full AEKF module.
