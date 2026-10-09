# Section Control verification

Run from the `ADCP Traveler` directory:

```powershell
python test/SectionControl/run_tests.py
node test/SectionControl/test_ui.js
node test/SectionControl/test_app.js
```

The host runner uses the installed MSYS2 compiler at `C:/msys64/ucrt64/bin/g++.exe` and writes its executable into the ignored `.pio/section-host-tests` directory. It compiles the production planner, workflow, encoder, motor controller, motion profile and distance controller. Only GPIO, PWM writes, interrupt registration, serial logging and time are stubbed. Encoder pulses exercise the actual distance controller, including settling and timeout handling.

Node tests exercise the production UI renderer and app script. They verify staged actions, offline gating, STOP availability during delayed responses, proportional widths, colour mapping, completion, status ordering, third-tab navigation and independent detail-strip scrolling. Direct Node invocation avoids spawning isolated test processes in restricted Windows sandboxes.

Build firmware and web assets with the installed PlatformIO CLI:

```powershell
& 'C:/Users/caela/.platformio/penv/Scripts/platformio.exe' run
& 'C:/Users/caela/.platformio/penv/Scripts/platformio.exe' run -t buildfs
```

Both firmware and SPIFFS must eventually be uploaded to use the feature on the ESP32. This implementation session builds them without uploading.

## Physical checks

After loading firmware and assets, verify the scan's fixed four-second ramp, encoder span measurement, immediate motor-output STOP, both return directions, interrupted section resume and final completion on the actual traveller. Inspect the UI on the intended phone, including short screens and horizontal scrolling at N=60. Software tests cannot establish real braking/coasting distances or physical endpoint accuracy.

## Control ownership

Power OFF and either existing STOP control pause Section Control. A manual speed command, an independent distance move or encoder zero discards a stopped section plan because its position reference changes. New Scan clears the workflow while stationary. Plans and progress survive browser reloads and reconnects while firmware remains running; they do not survive an ESP32 reboot.
