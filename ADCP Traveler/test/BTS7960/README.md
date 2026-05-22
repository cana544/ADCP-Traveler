# BTS7960 Arduino IDE Motor Test

This folder is for testing a BTS7960 motor driver with the ESP32.

## ESP32 to BTS7960 wiring

| BTS7960 pin | ESP32 pin      | Notes                                                         |
| ----------- | -------------- | ------------------------------------------------------------- |
| `RPWM`      | GPIO 25        | Clockwise PWM input                                           |
| `LPWM`      | GPIO 26        | Counter-clockwise PWM input                                   |
| `R_EN`      | GPIO 27        | Right-side enable, held HIGH in the sketch                    |
| `L_EN`      | GPIO 14        | Left-side enable, held HIGH in the sketch                     |
| `GND`       | GND            | Must share ground with ESP32 and motor supply                 |
| `VCC`       | 5V or 3V3      | Logic supply for the module, depending on your board revision |
| `B+`        | Motor supply + | External motor power, not ESP32 power                         |
| `B-`        | Motor supply - | External motor supply return                                  |
| `M+`        | Motor lead 1   | Motor output                                                  |
| `M-`        | Motor lead 2   | Motor output                                                  |

## What the sketch does

The sketch repeatedly ramps the motor slowly from 0 to 90% of max speed in the clockwise direction, returns to 0, then repeats the same ramp in the counter-clockwise direction.

## How to use

1. Open `BTS7960.ino` in the Arduino IDE.
2. Wire the driver as shown above.
3. Upload to the ESP32.
4. Open Serial Monitor at 115200 baud to watch the ramp stages.

## Tuning

- `TARGET_MAX_PERCENT` controls the peak speed.
- `RAMP_STEP_PERCENT` controls how smooth the ramp is.
- `STEP_DELAY_MS` controls how slow the ramp moves.
