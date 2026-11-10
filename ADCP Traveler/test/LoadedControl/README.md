# Loaded-control tests and bench tools

The host checks compile the production controller, encoder, motor driver, section integration and bounded IMU/USB diagnostic code. The dynamics model is actuator-responsive but uncalibrated; it is not a prediction of towing performance.

## Run the checks

Build once with PlatformIO to install the pinned sensor/JSON libraries, then run:

```powershell
python test/LoadedControl/run_tests.py
python -m unittest discover -s test/LoadedControl -p test_capture.py
python -m unittest discover -s test/LoadedControl -p test_unloaded_reporting.py
```

The host C/C++ runners currently use the Windows MSYS2 compiler installation at `C:/msys64/ucrt64/bin`. The UI and Section Control checks are listed in [the commissioning guide](../../docs/loaded-motion-testing.md).

## Capturing and plotting

Production USB commands provide LOG, STOP, adaptation switches and IMU diagnostics; they do **not** start movements. Use `scripts/capture_loaded.py` with the existing app for production captures. Opening the serial port may reboot the ESP32 and clears RAM learning.

The unloaded-study helpers use a disposable project with TEST MOVE/PING commands and a two-second heartbeat stop. `prepare_unloaded.py` and `prepare_retune_variant.py` preserve the original experiment preparation; their default paths identify historical studies. Read the scripts and choose a fresh project/results location before reuse. Never point a new capture or preparation step at an existing evidence folder. `unloaded_interface_test.cpp` exercises the temporary interface, not production firmware.

Install `pyserial`, `numpy` and `matplotlib` for hardware capture and graphs. Recreate the full reports without hardware access:

```powershell
python test/LoadedControl/plot_retuning.py test/LoadedControl/results/retune-2026-11-10
python test/LoadedControl/plot_unloaded.py test/LoadedControl/results/unloaded-2026-11-10
```

Raw measurements, CSV/JSON metrics, settings, source hashes, patches and findings remain in Git. Full HTML reports, per-trial graphs and firmware binaries are generated/local artifacts and ignored; three final comparison figures are retained for quick review. Historical reports retain their original endpoint policies and results.
