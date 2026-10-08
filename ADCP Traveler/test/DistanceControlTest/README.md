# Standalone distance control test

Edit `TARGET_DISTANCE_CM` and `TARGET_DIRECTION` in `DistanceControlTest.ino`.
Build with `platformio run --project-dir test/DistanceControlTest`, or open the
sketch in Arduino IDE. CSV output remains at 115200 baud.

The encoder's stop timeout is 500 ms. Speed is bounded by the age of the
latest pulse during deceleration, avoiding false zero-speed samples between
slow pulses. Low-speed drive uses the tracking voltage floor. PWM is removed
when position reaches half a pulse of the target (about 0.314 cm), followed
by stationary confirmation. Physical smoothness must be checked on the motor.

Host regression checks (from the repository root, with GCC installed):

```powershell
g++ -std=c++17 -Wall -Wextra -I test/DistanceControlTest/host test/DistanceControlTest/host/regression.cpp -o test/DistanceControlTest/host/regression.exe
& test/DistanceControlTest/host/regression.exe
```

These compile the actual sketch with simulated Arduino I/O and pulse timing;
they do not model motor friction or inertia.
