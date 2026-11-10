# Unloaded USB tests — 9 November 2026

Historical pause snapshot: testing paused at the user's request on 9 November. The user explicitly reauthorized testing on 10 November; see [the completed results](../unloaded-2026-11-10/README.md). Production firmware has now been restored and idle zero output verified.

At the pause, the current production build had been uploaded, followed by the approved temporary serial test build. The temporary build remained installed until testing resumed on 10 November. The saved production image is `production-firmware.bin`.

## Measurements completed

| Encoder-equivalent request | Direction | Result | Signed endpoint error | Duration | Recovery attempts |
|---|---|---|---|---|---|
| 10 cm | +1 | COMPLETE | -0.575222 cm | 8.300256 s | 0 |
| 10 cm | -1 | COMPLETE | -0.575222 cm | 8.897159 s | 0 |

Adaptation was disabled. Both moves satisfied the one-sided 0–1 cm encoder endpoint band. No additional encoder movement or nonzero PWM was observed during 2.5 seconds after the first terminal sample. Telemetry was sampled at approximately 5 Hz, with zero reported drops. This does not establish continuous physical motion, sub-pulse accuracy, or performance under load. The full distance/repeat/adaptation matrix and hardware STOP/heartbeat tests have **not** run. The unloaded control system is **not yet fully validated**.

The idle check recorded 51 zero-PWM samples with no encoder movement. The IMU returned no valid reports and its stationary-quality check obtained zero samples. IMU health remains unresolved.

## Prepared test artifacts

- `pilot.log` and `pilot.json`: raw serial evidence and measured results.
- `idle.log`, `idle-quality.log`, `paused-idle.log`: idle and stationary-check evidence.
- `temporary-interface.patch`: exact test-only changes to the diagnostic parser/handler.
- `tested-source.json`, `production-source-hashes.json`, `temporary-firmware-sha256.txt`: source and firmware identity.
- `production-firmware.bin`: production image saved before the temporary test build.
- `pilot-port-busy.log`: empty artifact from a port-open attempt before upload released COM3; no move was issued by that attempt.

Preparation code is `test/LoadedControl/prepare_unloaded.py`; serial capture/matrix code is `test/LoadedControl/run_unloaded.py`; host interface checks are `test/LoadedControl/unloaded_interface_test.cpp`. The disposable project is `.pio/unloaded-serial-test`. Production distance-controller, encoder, motor-driver, and Wi-Fi update-loop files are byte-identical in that copy. No production source changes were made for these tests.

Host interface checks passed after first demonstrating the missing MOVE behavior failed. They cover malformed/out-of-range input, active-move rejection, heartbeat expiry, STOP, and no restart from a later PING. Existing controller, sensor-transport, section-control, UI, battery, and capture-tool checks passed. Capture-tool tests required execution outside the Windows sandbox permission restriction. Both production and temporary PlatformIO builds passed; the clean temporary build emitted dependency deprecation warnings.

The computer's original Wi-Fi was restored after a brief connection to the traveller before the user's USB-only instruction. No Wi-Fi motion commands were sent. All measured moves used USB serial.
