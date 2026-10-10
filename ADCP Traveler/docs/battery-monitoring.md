# Battery monitoring

GPIO34 (ADC1), 47 kohm / 10 kohm divider, 100 nF to ground, common battery/ESP32 ground.
The installed ESP32 Arduino framework supports calibrated analogReadMilliVolts.
12-bit ADC at 11 dB attenuation accommodates the 2.21 V input at 12.6 V.

All adjustable values and the ascending 3S charge table are in include/config.h,
namespace Battery. Sixteen conversions spaced at least 2 ms apart produce one
measurement per second. Each update performs at most one ADC conversion, after
motor control, without delay or a sampling loop. EMA alpha 0.1 smooths voltage
with approximately ten seconds response. The first valid reading seeds the
filter; any invalid batch clears validity and the next valid batch seeds afresh.
Samples outside 6.0?13.2 V reconstructed terminal voltage or at ADC saturation
are rejected before filtering. State JSON reports false/null/null when invalid.
The existing shared status mutex protects sampling and serialization.

Charge is an estimate, not a coulomb counter. The initial discharge curve needs
validation under real loads and temperatures. No battery-dependent changes are
made to motor control or its nominal supply/feedforward constants.
GPIO34 has no internal pull resistors. With R2 present, a disconnected battery
reads ground and is unavailable. A bare floating pin on an unfinished board can
produce plausible noise; software cannot reliably prove a divider is physically
connected. Ground GPIO34 during bench testing without the divider. No simulated
values are used at runtime.

## Software checks

- python test/BatteryMonitor/run_tests.py
- node test/battery_ui_check.js
- node test/SectionControl/test_app.js
- PlatformIO run and run --target buildfs

## PCB verification

1. Upload firmware and SPIFFS assets (PlatformIO run --target upload and
   run --target uploadfs). Connect to Gauge Glide Traveller and open the interface.
2. Confirm resistor values/common ground. With battery disconnected and R2
   fitted, confirm grey empty icon and unavailable numeric readings.
3. Measure battery terminals and GPIO34 with a multimeter. At 12.6 V expect
   about 2.21 V at GPIO34. Compare the displayed terminal voltage after settling
   with the multimeter at several voltages. Set ADC_CORRECTION to measured
   terminal voltage / reported voltage if a consistent gain correction is needed.
4. With a suitable adjustable supply on the battery input, check table points
   and midpoints: 11.4 V gives 20% red, higher than 20% green; fill is proportional.
   Allow roughly 30 seconds after voltage steps for the filter to settle.
5. Run the motor in both directions and exercise distance, section, and STOP
   controls. Confirm updates continue and short voltage dips are smoothed.
   Test browser disconnection/reconnection and grounded ADC unavailable state.
6. Record a controlled discharge under representative loads and revise the
   lookup table against measured capacity; respect the battery/BMS cutoff.
