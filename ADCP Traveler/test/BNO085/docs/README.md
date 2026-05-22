# BNO085 Test Folder

Structure and workflow for IMU calibration and plotting.

- `firmware/` — ESP32 sketches used for capturing calibration data.
- `scripts/` — Python scripts for calibration, plotting and live tracking.
- `data/` — CSV captures (store full captures here; keep root clean).

Workflow:

1. Flash `firmware/BNO085.ino` to the ESP32.
2. Capture six static accel poses: `X+`, `X-`, `Y+`, `Y-`, `Z+`, `Z-`.
3. Keep each pose still for 10–30 s and save each capture as its own CSV.
4. Merge the six pose files with `scripts/merge_static_pose_csvs.py` to create `data/accel_static_poses.csv`.
5. Fit either the simple bias/scale model with `scripts/accel_calibrate.py` or the full 3x3 model with `scripts/accel_full_fit.py`.
6. Apply the fitted calibration in firmware and verify the corrected accel norm is close to `g` on all six static poses.
7. Use `csv_3DPlot.py` and `plot_BNO085_csv.py` to inspect traces before moving on to live motion tests.

TODO: BNO085 calibration plan, broken into the same major steps used in the ICM workflow.

1. Data gathering
   - Goal: capture raw sensor data that is actually used to solve the calibration.
   - What to record: one file per static face, with `Time_s, RawAccelX, RawAccelY, RawAccelZ`.
   - How to do it: place the board flat on each face, wait for the reading to settle, then record a short still window.
   - How much data: enough samples for a stable mean; 10–30 s per pose is usually enough.
   - Quality check: discard a capture if the board was bumped, rotated, or drifted during the hold.

2. Pose labeling and merge
   - Goal: turn separate captures into one labeled calibration table.
   - What it includes: the six gravity-aligned faces `X+`, `X-`, `Y+`, `Y-`, `Z+`, `Z-`.
   - How to do it: rename the files to those pose names and merge them with `scripts/merge_static_pose_csvs.py`.
   - Why the label matters: the fit scripts use the pose label to match each measured vector to the expected gravity vector.

3. Estimate calibration values
   - Goal: compute the numbers that go into the correction model.
   - Simple model: `a_true = (a_meas - bias) / scale`.
   - How to get `bias`: for each axis, take the mean reading on the positive face and the mean reading on the negative face, then use `0.5 * (mean(+face) + mean(-face))`.
   - How to get `scale`: use the same two means and divide their difference by `2g`.
   - Full model: `a_true = M @ a_meas + b`.
   - How to get `M` and `b`: run `scripts/accel_full_fit.py` on the merged pose CSV; it solves a least-squares system from the measured vectors to the expected gravity vectors.
   - Data requirement: this step only works after the six pose captures are complete and labeled.

4. Apply the calibration in firmware
   - Goal: use the corrected acceleration everywhere else in the pipeline.
   - What it includes: replacing raw accel with corrected accel before filtering, integration, and motion detection.
   - If using the simple model: subtract the bias and divide by scale on each axis.
   - If using the full model: multiply the raw vector by `M` and add `b`.
   - Where the values live: store them in JSON or a header file, then copy them into the firmware constants.

5. Verify the result
   - Goal: prove the calibration actually worked.
   - What to check: the corrected accel magnitude should sit near `g` when the board is still.
   - What to plot: raw accel, corrected accel, and the corrected norm across all six poses.
   - Failure signs: large bias after correction, inconsistent face means, or a norm that is far from `9.80665 m/s^2`.

6. ICM-style stillness calibration and gating
   - Goal: match the same working pattern used in the ICM test sketch.
   - Initial calibration window: average the first few seconds while the board is motionless to estimate gyro bias and gravity bias.
   - Stillness gate: only treat the capture as valid if the accel norm is close to `g`, the norm variance is small, and the gyro norm stays low.
   - Motion integration: after bias correction, integrate gyro for angle and linear accel for velocity/displacement.
   - Verification step: reject captures that do not pass the stillness gate before using them as calibration data.

7. Magnetometer ellipsoid fit
   - Goal: calibrate heading by removing hard-iron offset and soft-iron distortion.
   - What to gather: raw magnetometer samples while slowly rotating the board through many orientations.
   - How to do it: record a dense 3D sweep, then fit the magnetometer cloud to an ellipsoid.
   - What you get: an offset vector plus a 3x3 correction matrix, similar in spirit to the accel matrix fit.
   - When to do it: after accel calibration is stable, before any heading or yaw fusion work.

8. AEKF / fusion layer
   - Goal: combine gyro, accel, and magnetometer into a stable orientation estimate.
   - What it includes: state prediction with gyro, measurement correction with accel/mag, and covariance tuning.
   - How calibration feeds it: accel correction should already be applied before the filter; mag data should also be ellipsoid-corrected first.
   - What to verify: yaw drift, response to turns, stillness stability, and whether the filter rejects brief motion spikes.
   - When to do it: only after accel and magnetometer calibration are both repeatable.
