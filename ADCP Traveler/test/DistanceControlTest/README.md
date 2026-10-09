# Command-driven distance-control tuning

This standalone ESP32 sketch boots idle and accepts newline-terminated commands
at 115200 baud. The unloaded shaft is on COM3; equivalent travel is 10–500 cm.

## Build and upload

```powershell
platformio run --project-dir test/DistanceControlTest
platformio run --project-dir test/DistanceControlTest --target upload --upload-port COM3
```

Close other serial monitors first.

## Commands

```text
HELLO
STATUS
CONFIG 20 10 3 13 2 23 1.8
MOVE 50 1
PING
STOP
```

CONFIG order: vmax cm/s, amax cm/s², position Kp, velocity Kp, velocity Ki,
feedforward V/(m/s), friction compensation V. MOVE uses distance cm and
+1 for CW or -1 for CCW. Configuration changes are rejected while moving.
The capture script sends heartbeats automatically; a gap over 2 seconds stops
PWM. The trial deadline also stops PWM. The controller stops at the planned
profile end and reports COMPLETE or ENDPOINT_ERROR after stationary confirmation.
It never restarts to correct the endpoint; a failed trial permits a later trial.

Initial common limits: 20 cm/s, 10 cm/s². Profiles are triangular below 40 cm
and trapezoidal above it, with zero cruise at 40 cm. Encoder resolution and
accepted final error are one pulse, about 0.628 cm.

The frozen shared configuration is also saved in `selected_settings.json`:
position Kp=3, velocity Kp=13, velocity Ki=2, feedforward=23 V/(m/s), friction
compensation=1.8 V. Derivative gain is zero. The encoder uses a fixed 15 ms
debounce floor and a median of three pulse periods; elapsed pulse age bounds
the velocity estimate during deceleration. No relative pulse-rejection rule
is used, so a speed change cannot lock out every other encoder pulse.

## Capture and report

Requires pyserial, numpy and matplotlib.

```powershell
python test/DistanceControlTest/scripts/tuning.py run --distances 500 100 10 --stage baseline
python test/DistanceControlTest/scripts/tuning.py run --distances 50 --settings settings.json
python test/DistanceControlTest/scripts/tuning.py run --distances 500 250 100 75 50 30 25 20 10 --directions 1 -1 --repeats 3 --settings settings.json --stage validation
python test/DistanceControlTest/scripts/tuning.py report
```

Settings JSON overrides named configuration values. Every trial saves raw log,
CSV, settings/metrics JSON, four-panel trace and pulse-timing graph.
`results/index.html` links every run, a tuning-progress dashboard, final metric
and velocity-profile dashboards, and baseline/final comparisons. Failed trials
remain visible. Numerical screening complements visual graph review and does
not prove the absence of sub-pulse jitter or performance under load.

## Verification

```powershell
g++ -std=c++17 -Wall -Wextra -I test/DistanceControlTest/host test/DistanceControlTest/host/regression.cpp -o test/DistanceControlTest/host/regression.exe
& test/DistanceControlTest/host/regression.exe
python -m unittest discover -s test/DistanceControlTest/scripts -p test_tuning.py
node test/ui_layout_check.js
g++ -std=c++17 -Wall -Wextra -I test/DistanceControlTest/host -I include test/DistanceControlTest/host/app_regression.cpp -o test/DistanceControlTest/host/app_regression.exe
& test/DistanceControlTest/host/app_regression.exe
```

C++ tests compile the real sketch with simulated Arduino I/O. They check
profile area/bounds, idle boot, sparse encoder pulses, command rejection,
timeouts, cancellation and recovery. Hardware trials measure motor response.
