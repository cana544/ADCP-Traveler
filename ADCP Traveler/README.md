# ADCP Traveller

ESP32 firmware and the existing web app for manual, distance and automated section movement.

The operational distance controller uses a fixed target, startup effort ramp, position-based braking, velocity PI/feedforward, direction-specific RAM learning and bounded recovery. Current common settings are Kp=13, Ki=4, feedforward=17.5 nominal V/(m/s), friction compensation=2.3 nominal V and a 3 cm/s approach request. Completion requires a stationary encoder endpoint within ±0.5 cm. The original Speed, Distance and Section Control app files are retained.

## Build and flash

Install PlatformIO and run from this directory:

```powershell
pio run
pio run -t upload --upload-port COM3
pio run -t uploadfs --upload-port COM3
```

Use the actual USB port. Firmware and app filesystem uploads are separate; `uploadfs` installs the files in `data`. Build output stays in ignored `.pio` directories.

## Verification and evidence

- [Commissioning, USB logging and verification commands](docs/loaded-motion-testing.md)
- [Controller test tools](test/LoadedControl/README.md)
- [Retuning findings and measured validation](test/LoadedControl/results/retune-2026-11-10/README.md)
- [Measured comparison chart](test/LoadedControl/results/retune-2026-11-10/plots/comparison.png)

The selected settings passed 56 unloaded encoder-based moves across sampled 5–500 cm requests in both directions without recovery restarts, plus five STOP/heartbeat checks. Loaded towing performance and physical ±0.5 cm accuracy remain unvalidated. The IMU currently returns no valid reports and is diagnostic only; it does not control motion.
