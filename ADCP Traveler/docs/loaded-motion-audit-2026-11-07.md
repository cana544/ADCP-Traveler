# Loaded-motion software audit — 7 November 2026

Historical review of the firmware, configuration, host tests, scripts and documentation against the 7 November design brief. Duplicate proposal copies and execution ledgers have since been archived locally under ignored `.pio` storage. Their earlier “proposed / firmware unchanged” wording and build-progress checkboxes do not describe the current operational controller. Use [the commissioning guide](loaded-motion-testing.md) and the latest measured results for current behaviour; this audit preserves the findings from its original review.

## Corrections made during this audit

- Startup/recovery effort ramps previously bypassed the stopping envelope. They now stop with `BRAKING_LIMIT` when measured speed exceeds that envelope before PI handoff. A regression using real encoder pulses reproduced both missing guards before the fix.
- Starting-effort and running-load adaptation previously shared one switch. They now have independent idle-only controls, `ADAPT START ON/OFF` and `ADAPT LOAD ON/OFF`; existing `ADAPT ON/OFF` controls both. Tests cover separate learning, different directional loads, preserved opposite-direction memory and rejection during active motion.
- USB `applied_v` previously came from DistanceController and falsely reported zero during manual motion. It now follows the actual motor PWM command. Added mode, transition reason, braking-model limitation and individual adaptation-switch fields. The real ArduinoJson serializer is now host-tested, including dropped samples under TX backpressure and command handling.
- Capture reporting now rejects malformed optional counters/text and prevents manual/scan movement or subsequent idle snapshots from becoming a previous automatic move's endpoint. Regressions reproduced the reporting defects before the fixes.
- Added actuator-responsive assisted-motion, absolute-deadline-through-recovery and startup-timeout checks, plus STOP tests that explicitly reach STARTING, MOVING, APPROACHING, SETTLING and RECOVERING.

## Requirements and file coverage

| Area / reviewed files | Evidence and result |
| --- | --- |
| `config.h`, `distance_controller.h/.cpp` | Online position request, original target across startup/recovery, one-sided settling/crossing failure, finite retries, progress and absolute deadlines, bounded PI/learning, no commanded reverse. Loaded dynamics, timeout, wrap, phase STOP, learning and ramp-protection checks pass. |
| `encoder.h/.cpp`, `motor_controller.h/.cpp` | Distance-only pulse-aware stop interval; manual estimator retained. Continuous effort slew accumulator and quantized actuator feedback retained. Slow-pulse, frozen encoder/reference and output-bound checks pass. |
| `section_controller.cpp`, `wifi_hotspot.h/.cpp`, `main.cpp` | Shared DistanceController, anchored targets, strict section crossing checks, existing ownership/manual/scan workflow, control before optional diagnostics and no catch-up bursts. Section, app/UI tests and ESP32 build pass. These host/UI tests do not constitute live browser/hardware commissioning. |
| `bno_uart_transport.h`, `bno_shtp_guard.h`, `imu_data.h`, `imu_monitor.h/.cpp`, `platformio.ini` | Pinned BNO08x 1.2.5, bounded UART reads/writes and malformed-frame guard, independent report freshness, resets and stationary-quality diagnostics. Parser/state tests and actual pinned SH-2 runtime tests pass. IMU has no motion or adaptation authority. |
| `diagnostic_commands.h`, `control_diagnostics.h/.cpp` | Bounded commands, no MOVE command, idle-only adaptation changes, best-effort JSON output/drop counters, actual PWM-equivalent output and explicit mode. Real serialization/command tests pass. |
| `scripts/capture_loaded.py`, `prepare_baseline.py`, `baseline_diagnostics.inc` | Bounded capture, STOP on normal completion/interruption, no movement commands, separate physical/encoder errors, baseline controller blob preserved, malformed/incomplete/cross-boot and unrelated-motion reporting handled. Seven Python tests and isolated baseline build pass. Capture hardware I/O was not exercised. |
| `test/LoadedControl/*`, modified DistanceControlTest host tests/shim, `test/SectionControl/test_sections.cpp` | Read and run, including new diagnostics integration test. Frozen standalone `.ino`, MotionProfile utility and historical result files retained. |
| Commissioning guide, both design copies, repository implementation plan/progress ledger | Read against implementation. Commissioning guide updated for audit corrections. Original brief/history retained. This note records current verification. |

## Fresh verification

All commands below completed successfully during this audit:

```text
python test/LoadedControl/run_tests.py
python -m unittest discover -s test/LoadedControl -p test_capture.py       # 7 tests
python test/SectionControl/run_tests.py
node test/SectionControl/test_ui.js                                      # 12 tests
node test/SectionControl/test_app.js                                     # 7 tests
python test/BatteryMonitor/run_tests.py
python -m unittest discover -s test/DistanceControlTest/scripts -p test_tuning.py  # 9 tests
platformio run
platformio run -t buildfs
platformio run --project-dir .pio/loaded-motion/baseline-source
git diff --check
```

PlatformIO executable: `C:/Users/caela/.platformio/penv/Scripts/platformio.exe`. Final revised firmware uses 55,956 / 327,680 bytes RAM (17.1%) and 909,653 / 1,310,720 bytes flash (69.4%). The first full build emitted existing ESPAsyncWebServer/AsyncTCP deprecation warnings; it succeeded. Git reports existing LF/CRLF conversion notices; the whitespace check passes.

The first sandboxed Python runs failed on Windows temporary-folder permissions. Both suites passed when rerun with temporary-folder access; these errors were not attributed to controller behavior. PlatformIO similarly required access to its installed packages/lock files outside the workspace.

Original controller reference: `5b1e4c310e9129574d01ef064bc92a3ca8219cb1`. Prepared baseline controller matches the committed blob byte for byte, SHA256 `179667185c240f7d994901ccdc21012ee19a49b566e3381b9f60aa70dcd800b6`. The preserved source archive matches after line-ending normalization. The frozen standalone sketch has no working-tree diff. Baseline firmware compilation succeeds; its relation to firmware currently on a physical device remains unverified.

An independent read-only review found the missing coverage and a late endpoint-attribution edge case; both were addressed with regressions. No further file removals were justified.

## Limited cleanup

Removed five production constants with no remaining references: `POSITION_KP`, `VELOCITY_KD`, `MOVE_TIMEOUT_MARGIN_S`, `VELOCITY_TOLERANCE_CM_S`, `COMPLETE_CONFIRM_CYCLES`. Removed nine inspected empty test temporary directories that caused Git permission warnings. No source module, design copy, baseline artifact, historical result or useful test was deleted. Changes remain uncommitted on `feature/loaded-motion-control`.

## Remaining experimental acceptance

Software checks found no regression in the exercised cases. They do not establish physical ≤1 cm accuracy, absence of rope slip/reverse rolling, effective braking at zero PWM, IMU mounting/quality, or loaded river performance. No upload or hardware actuation was performed.

Normal PI travel may temporarily have filtered measured speed above the planning envelope; `braking_limited` exposes this. A trial blanket fault on this condition falsely aborted achievable synthetic short moves, so that policy was not retained. Normal travel retains its speed/effort slew, zero-room request reduction, strict measured crossing failure and time/progress bounds. The braking constants remain commissioning assumptions, not proof of physical stopping capability. Follow `loaded-motion-testing.md` to measure baseline, adaptation-disabled and adaptation-enabled runs on the real rig before accepting performance or changing these limits.
