# Loaded-motion controller: commissioning and verification

The revision replaces the production fixed-duration trajectory with an online position-based request. The frozen standalone DistanceControlTest and its historical results are unchanged. Section Control uses the revised DistanceController; its scan ramp and section layout remain unchanged.

Software checks and synthetic dynamics tests do **not** establish the physical 1 cm requirement. No firmware is automatically uploaded, and none of the scripts starts a movement. Commission on the home rig before river use.

## What changes

- Normal velocity requests accelerate at up to 10 cm/s² and decelerate at a conservative 5 cm/s², with maximum requested speed 20 cm/s. These are reference limits, not guaranteed physical acceleration bounds.
- The online braking envelope includes measured speed, a 0.25 s reaction allowance and a 0.3 cm margin to the far accepted edge of the ±0.5 cm window. The original absolute target remains fixed. Within 8 cm, requested speed is limited to 3 cm/s and reduced further by the envelope. These defaults require loaded measurement.
- The target is never shifted during startup, approach or recovery. Success requires a stationary encoder position in the **symmetric ±0.5 cm band around the target**. Slight target crossing inside the band is accepted; crossing more than 0.5 cm beyond the target fails without commanded reversal. This policy was selected on 10 November 2026; the preceding 60-move unloaded report used the earlier one-sided band and does not physically validate this narrower band.
- Lost encoder progress for 2 s starts a bounded recovery. There is a 0.7 s pause, then an effort ramp at 0.5 nominal V/s up to 6 nominal V. Each recovery lasts at most 12 s after the pause; at most two are allowed. Startup remains limited to 8 nominal V and 10 s.
- An absolute move budget of 40 s + distance(cm)/2, capped at 600 s, includes all recoveries. A gap greater than 250 ms in active control fails the move instead of catching up. All values are configurable in include/config.h.
- Explicit STOP/power OFF/fault/target protection removes effort immediately. Routine motion uses limited speed and voltage changes. A detected obstruction or crossing beyond the endpoint band takes precedence over smoothness.
- Startup/recovery effort ramps fail with `BRAKING_LIMIT` if measured speed exceeds the braking envelope before handoff. During normal PI travel, `braking_limited` records excess measured speed over the planning envelope; filtered pulse velocity can lag during deceleration, so this flag alone is not a physical overrun measurement. Validate stopping with rope marks.
- A slow pulse train uses a pulse-aware distance-mode stop interval: at least 0.5 s, up to twice the latest period, capped at 2 s. Manual-mode filtering and timeout remain unchanged. Pulse age and count are always logged.

## Adaptation

Unloaded continuous-move retuning selected velocity Kp=13, Ki=4, feedforward=17.5 nominal V/(m/s), and friction compensation=2.3 nominal V. The same calibration applies across tested distances and directions; distance alone is not used to infer load. Startup breakaway effort is not enforced as a hard voltage floor during deceleration, because doing so could override stopping protection. The running-load bias and PI correction remain active during approach; bounded recovery remains available for genuine lost progress. See `test/LoadedControl/results/retune-2026-11-10` for baseline, candidates, validation and limitations.

Adaptation is enabled at boot, but begin commissioning with `ADAPT OFF` for a controlled comparison. The velocity integral still operates when adaptation is off.

Learning is direction-specific and RAM-only. Startup captures a wheel-breakaway candidate; it is accepted only after stable travel, with a 0.15 update fraction and a 6 V bound. The learned value guides the low part of a gradual startup ramp, never a voltage jump. Running-load bias slowly absorbs part of the PI integral, at no more than 0.15 V/s within ±3 V, without adding the same correction twice. Eligibility excludes approach/recovery, saturation/slew limitation and unstable or stale motion. PI anti-windup accounts for actual quantized PWM-equivalent output.

`ADAPT OFF`, `ADAPT ON` and `ADAPT RESET` are accepted only when automatic motion is inactive. Reset clears both directions; reboot also clears all learned values. Start with a reset after changing setup. Learning detects wheel rotation, not proof of body movement; undetected slip can corrupt its observations. It never increases speed, acceleration, startup/recovery caps or retry limits.

The two learned quantities can also be switched independently with `ADAPT START ON/OFF` and `ADAPT LOAD ON/OFF`. `ADAPT ON/OFF` switches both; reset clears stored values without changing the switches. Telemetry includes `adapt_start` and `adapt_load`.

## BNO085

The existing UART connection uses ESP32 RX16/TX17. UART-SHTP runs at **3 Mbaud**, also the rate selected by the stock Adafruit UART implementation. USB diagnostics remain 115200 baud.

The pinned Adafruit BNO08x 1.2.5 dependency supplies SH-2 decoding. Production uses a separate bounded UART HAL because the stock UART HAL waits indefinitely for missing bytes. Each read consumes at most 96 available bytes, preserving partial frames. Oversized/malformed frames are dropped. TX backpressure returns a negative error: SH-2 retries zero forever. Report configuration retries on later loops. Boot advertisement wait is at most 200 ms, before controls are served. Sensor absence, reset and stale data do not inhibit encoder-based control.

Requested reports are accelerometer (m/s², includes gravity), linear acceleration (m/s²), calibrated gyro (rad/s), and game rotation vector (quaternion x/y/z/w), each at 50 Hz. Every report has independent validity, host age, sensor timestamp and accuracy status. All reports must be fresher than 0.5 s for the diagnostic `healthy` flag. This is data freshness, not a slip-detection or accuracy guarantee. Game rotation vector avoids magnetometer-dependent heading; its yaw can drift.

No IMU data changes distance, motor commands or adaptation eligibility in this revision. Raw encoder counts remain authoritative. There is no double-integrated inertial position, acceleration-based count rejection, slip correction or inferred stationary body velocity.

Document mounting axes physically: while idle, log a stationary sample, then manually tilt/rotate the unpowered traveller to identify axes/signs. Do not assume the old test sketch's mounting orientation. `IMU CHECK` runs a diagnostic stationary-quality window of 150 accel samples paired with fresh gyro readings. It checks acceleration magnitude near gravity, magnitude noise and gyro norm. Keep the unit still while checking. It does not estimate a full calibration matrix, certify orientation, prove zero velocity or authorize movement.

## USB commands and logging

Use a serial terminal at 115200 baud, newline terminated:

```text
LOG ON
ADAPT OFF
ADAPT RESET
IMU CHECK
STOP
LOG OFF
```

There is intentionally no USB MOVE command. Use the existing app to move. `STOP` cancels distance and section movement, including pending recovery. Log/acknowledgment output is best-effort; logging overload drops samples rather than waiting for TX space. Overlong or unknown commands are rejected. Close other serial programs before opening the capture tool.

Full diagnostic snapshots are downsampled to **5 Hz**, rather than logging every 50 Hz IMU report. Each line is `GG,{JSON}` with schema version, boot/move/sequence identity, position, absolute target, phase/failure, requested velocity/effort, PWM-equivalent applied voltage, integral/load corrections, adaptation eligibility, budgets/retries, pulse age and per-report IMU data. `requested_v`/`applied_v` are nominal-voltage equivalents, **not measured motor voltage or torque**. No motor-current limit is claimed. Sensor reset and logger-drop counters are explicit. Missing or dropped samples limit transient analysis.

`mode` distinguishes IDLE, MANUAL, SCAN, DISTANCE and SECTION, and `transition_reason` describes the last controller transition. `applied_v` always follows the actual PWM command, including manual/scan output. Capture summaries exclude manual/scan samples from automatic-move metrics. Baseline records lack these newer fields and remain readable.

From the `ADCP Traveler` directory, with Python and pyserial installed:

```powershell
python scripts/capture_loaded.py capture --port COM3 --seconds 120 --output .pio/loaded-motion/trial-01.log
python scripts/capture_loaded.py report .pio/loaded-motion/trial-01.log
python scripts/capture_loaded.py measure --requested-cm 100 --actual-cm 99.5 --encoder-cm 99.3
```

Use the actual USB port. Opening a port can reboot the ESP32, clearing learning and section state: connect while stationary, then establish the app/scan. Capture sends LOG ON, and sends STOP/LOG OFF when it ends or is interrupted. Do not begin a move that you intend to continue beyond the capture deadline. The report groups by boot and move so a reboot cannot silently combine trials. It records incomplete runs, failures, sequence gaps and drop counts. Capture never overwrites an existing output file.

For both directions enter measurement magnitudes in the commanded direction. Measure actual displacement along rope marks from a consistent point on the traveller. Report signed physical error separately from encoder error and from wheel-minus-body displacement. Positive physical error is overshoot. Assess every physical endpoint against the intended physical accuracy requirement; a low average is insufficient. The firmware's ±0.5 cm acceptance is based on quantized encoder travel and does not prove physical ±0.5 cm accuracy. The measurement helper's named `one_sided_pass` field retains its legacy physical 1 cm/no-overshoot screening and is not the current firmware completion rule.

## Baseline before revised hardware commissioning

Original source is pinned at `5b1e4c310e9129574d01ef064bc92a3ca8219cb1`; the source archive is preserved locally in `.pio/loaded-motion/baseline-5b1e4c3.zip`. The previous local firmware binary, if present, is saved as `previous-local-firmware-unverified.bin`; its relation to the flashed device is not proven.

Prepare a logged baseline without changing its controller:

```powershell
python scripts/prepare_baseline.py
& 'C:/Users/caela/.platformio/penv/Scripts/platformio.exe' run --project-dir .pio/loaded-motion/baseline-source
```

The preparation script refuses to overwrite an existing destination. It retrieves committed source with git, adds read-only diagnostics and bounded USB LOG/STOP handling, and preserves the original distance_controller.cpp blob exactly. Original gains, timed motion and encoder handling are unchanged. It does not add adaptation or IMU control. Baseline records omit unavailable command-speed/PI/IMU fields rather than inventing them. Use the same capture tool and physical measurements. The baseline build and revised build are separate directories. Upload only as a deliberate owner-run commissioning step with the rig ready; no upload has been performed by the implementation workflow.

## Home test sequence

1. Establish usable travel and clearance at both ends of the 3–5 m rig. Use the actual field rope. Record span, attachment heights, approximate sag, dry/wet condition and the added load. Adding 3–5 kg tests vertical loading; it does not reproduce all boat drag/tether forces.
2. Record original-controller baseline moves, requested/encoder/physical distances and endpoint behaviour. Include short distances and longer moves fitting inside the usable clearance. Start without added mass, then increase load in manageable steps.
3. With the revised build, LOG ON, ADAPT OFF and ADAPT RESET. Verify idle IMU health and IMU CHECK. Begin with a short single move and inspect the full start/stop before increasing distance/load.
4. Repeat 10, 30, 50, 100 cm and longer safe moves in both directions, several repeats each. Record each physical result, duration, retries and fault reason. Investigate any crossing beyond the selected band before continuing.
5. Repeat the same matrix with ADAPT ON after reset, allowing longer stable runs to establish learning. Compare against disabled results on the same setup. A short move may not have a stable learning window and need not learn.
6. Verify STOP during startup, cruise, approach and recovery; confirm no delayed automatic restart. A blocked-drive test is optional and must be brief and supervised; do not defeat time/effort caps to force completion.
7. Run scan/section sequences in both directions. Confirm one GO completes an achievable section, unfinished moves retain their target and interrupted moves resume only when asked. Check physical station locations, not just the final return to origin.
8. Repeat representative cases after a setup-tension change and with the intended added load. Assess individual runs and worst error, not just a mean. Compare logs against video/marks when wheel and body travel disagree.

If the encoder endpoint is correct but the physical endpoint is not, do not solve that discrepancy by inflating travelled distance, increasing gains or rejecting counts using acceleration alone. Record the contact/slip limitation. Stopping margins, approach speed, PI gains and effort caps require real commissioning; zero overshoot across every setup cannot be promised from the software tests. Field towing validation remains necessary after home tests.

## Reproducible software checks

```powershell
python test/LoadedControl/run_tests.py
python -m unittest discover -s test/LoadedControl -p test_capture.py
python test/SectionControl/run_tests.py
node test/SectionControl/test_ui.js
node test/SectionControl/test_app.js
python test/BatteryMonitor/run_tests.py
python -m unittest discover -s test/DistanceControlTest/scripts -p test_tuning.py
& 'C:/Users/caela/.platformio/penv/Scripts/platformio.exe' run
& 'C:/Users/caela/.platformio/penv/Scripts/platformio.exe' run -t buildfs
```

Host compiler path is currently `C:/msys64/ucrt64/bin`. Install PlatformIO dependencies first so the real pinned SH-2 sources exist for the runtime test. Frozen standalone tuning tests retain their original profile assumptions; production startup tests now test online motion and loop-gap failure. Actuator-responsive synthetic tests exercise software logic; their results are not physical test measurements.
