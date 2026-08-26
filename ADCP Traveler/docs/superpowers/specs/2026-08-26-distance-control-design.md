# Distance Control Design

## Goal
Add relative distance control to the existing Gauge Glide ESP32 firmware while preserving the current manual speed mode and BTS7960 motor control.

## User workflow
- All operator-facing distance values use centimetres.
- The user enters a positive distance magnitude in cm.
- The user selects CW or CCW.
- CW is positive travel and CCW is negative travel, matching the existing signed MotorController convention.
- START begins a new relative movement from the traveller's current physical position.
- STOP immediately cancels the active move, sets motor PWM to zero, and disables both BTS7960 enable pins.
- ZERO establishes the current physical location as 0 cm and is unavailable during an active distance move.
- Manual speed commands automatically cancel any active distance move before manual control takes over.
- Encoder position is tracked continuously in both manual and distance modes and is only reset by ZERO.

## Fixed control parameters
- Maximum velocity: 50 cm/s.
- Maximum acceleration: 25 cm/s^2.
- Drive wheel radius: 5 cm.
- Encoder: single-channel, GPIO 21, 50 slots/revolution, rising-edge interrupt.
- Distance per pulse: 2*pi*5/50 = 0.62831853 cm/pulse.
- Position gain Kx: 1.0 1/s.
- Velocity feedback initial gains: Kp = 10, Ki = 0, Kd = 0.
- Motor supply limit: +/-12 V.
- Feedforward gain: Kff = (Km + Rw*B/Km)/r using Rw = 0.3918 ohm, Km = 1.216, B = 0.4098 and r = 0.05 m. The embedded implementation stores the resulting constant and converts the requested voltage to signed PWM duty.

## Control architecture
The distance controller preserves the current Simulink architecture:

1. A triangular or trapezoidal motion profile generates xref and vref from elapsed move time, commanded relative distance, fixed vMax and fixed aMax.
2. Relative measured position for the active move is measured from the encoder position captured at START.
3. Position correction is deltaVelocity = Kx * (xref - xMeasuredRelative).
4. vCommand = clamp(vref + deltaVelocity, -vMax, +vMax).
5. Velocity feedback uses the measured encoder velocity and the configured PID gains.
6. Motor feedforward is Uff = Kff * vref.
7. Ueffort = clamp(Ufb + Uff, -12 V, +12 V).
8. Ueffort is converted to signed BTS7960 PWM through the existing MotorController.

## Encoder design
The encoder ISR records total pulses, the time of the latest pulse, and the latest pulse period. Position and velocity calculations are performed outside the ISR.

Because the encoder is single-channel, direction cannot be measured directly. During a distance move, encoder sign is locked to the selected movement direction for that move. During manual control, signed motor commands provide the active direction, but reversal is only permitted after the encoder confirms the traveller has stopped. A stop is inferred when no encoder pulse has arrived for the configured stop timeout.

Measured velocity is calculated from pulse period and converted to cm/s. If no pulse arrives within the stop timeout, measured velocity is forced to zero. A small four-sample moving average is applied to the raw velocity to match the filtering intent of the current Simulink model without reproducing unnecessary simulation blocks.

## Move completion and cancellation
A distance move does not complete merely because the profile time has elapsed. After the profile finishes, the controller remains active until measured position is within an encoder-compatible tolerance and measured velocity is effectively zero for consecutive control updates. The initial position tolerance is one encoder pulse.

STOP cancels the move completely and does not preserve any remaining distance command. A later START begins a new relative move from the traveller's then-current position.

If position correction would require a direction reversal after overshoot, the controller first stops and confirms zero motion. Any opposite-direction correction is treated as a new correction phase so encoder sign is never changed while the wheel may still be moving in the previous direction.

## Software structure
Existing files are retained where practical.

New modules:
- include/encoder.h and src/encoder.cpp: pulse ISR support, signed position, velocity, zeroing, stop detection and filtering.
- include/motion_profile.h and src/motion_profile.cpp: triangular/trapezoidal reference generation.
- include/distance_controller.h and src/distance_controller.cpp: distance-mode state machine and control equations.

Modified modules:
- include/config.h: encoder, motion and controller constants.
- include/motor_controller.h and src/motor_controller.cpp: expose direction/state helpers and voltage-to-PWM entry point while preserving existing signed speed API.
- include/wifi_hotspot.h and src/wifi_hotspot.cpp: own the encoder and distance controller, add distance-mode HTTP/WebSocket commands, cancel distance mode before manual commands, and broadcast live position/status.
- data/index.html, data/script.js and data/style.css: minimal distance-mode controls for distance, CW/CCW, START, STOP, ZERO, current position and status.
- src/main.cpp remains intentionally small.

## Safety behaviour
- STOP always has priority over motion commands.
- STOP sets PWM to zero and disables REN and LEN.
- START re-enables the driver before movement.
- ZERO is rejected during active movement.
- Manual commands cancel distance mode before reaching MotorController.
- Invalid or zero distance commands are rejected.
- Direction reversal is blocked until stop detection is true.

## Scope
This change does not add absolute-position moves, editable vMax/aMax controls, quadrature direction sensing, IMU-based direction estimation, or unrelated UI features.