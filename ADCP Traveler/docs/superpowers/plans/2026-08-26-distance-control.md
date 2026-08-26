# Distance Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add safe relative distance control in centimetres to the existing ESP32 Gauge Glide firmware while preserving manual speed control.

**Architecture:** Add dedicated encoder, motion-profile and distance-controller modules. WifiHotspot remains the user-interface boundary and delegates motion to the controller; MotorController remains the BTS7960 hardware abstraction. Encoder position is continuous across modes, with distance moves using a captured start position and locked move direction.

**Tech Stack:** PlatformIO, Arduino framework for ESP32, ESPAsyncWebServer, ArduinoJson, SPIFFS, C++.

**Spec:** `docs/superpowers/specs/2026-08-26-distance-control-design.md`

## Global Constraints
- Operator-facing units are cm, cm/s and cm/s^2.
- vMax = 50 cm/s.
- aMax = 25 cm/s^2.
- Encoder is single-channel on GPIO 21, 50 slots/revolution, rising edge.
- CW is positive, CCW is negative.
- STOP cancels the move, commands zero PWM and disables REN/LEN.
- ZERO is disabled while moving.
- Manual speed commands cancel distance mode.
- Encoder position persists across modes until ZERO is pressed.
- Direction reversal is not accepted until the encoder indicates stopped.

---

### Task 1: Add shared control configuration

**Files:**
- Modify: `include/config.h`

**Interfaces:**
- Produces constants under `Config::Encoder`, `Config::Motion`, and `Config::Control` used by later modules.

- [ ] Add GPIO 21, 50 slots/rev, 5 cm wheel radius, derived distance-per-pulse, stop timeout, 50 cm/s vMax, 25 cm/s^2 aMax, Kx=1.0, Kp=10, Ki=0, Kd=0, supply limit 12 V and Kff.
- [ ] Build with `pio run` and confirm existing code still compiles.
- [ ] Commit `feat: add distance control configuration`.

### Task 2: Implement encoder module

**Files:**
- Create: `include/encoder.h`
- Create: `src/encoder.cpp`

**Interfaces:**
- Produces `Encoder::begin()`, `Encoder::update()`, `Encoder::setDirection(int)`, `Encoder::zero()`, `Encoder::positionCm()`, `Encoder::velocityCmS()`, `Encoder::isStopped()`, `Encoder::pulseCount()`.

- [ ] Implement a rising-edge ISR that only records pulse count/timing.
- [ ] Implement signed pulse accumulation using a direction value held stable across a move.
- [ ] Implement period-based cm/s conversion, stop timeout and four-sample moving average outside the ISR.
- [ ] Implement zero offset without clearing physical pulse history.
- [ ] Build with `pio run`.
- [ ] Commit `feat: add single channel encoder module`.

### Task 3: Implement triangular/trapezoidal motion profile

**Files:**
- Create: `include/motion_profile.h`
- Create: `src/motion_profile.cpp`

**Interfaces:**
- Produces `MotionReference { positionCm, velocityCmS, finished }` and `MotionProfile::start(distanceCm, direction)`, `MotionProfile::sample(elapsedSeconds)`.

- [ ] Port the validated MATLAB triangular/trapezoidal equations to C++ using fixed vMax and aMax.
- [ ] Preserve sign through the selected CW/CCW direction rather than accepting negative distance input.
- [ ] Ensure the final sample returns exact commanded position, zero velocity and `finished=true`.
- [ ] Build with `pio run`.
- [ ] Commit `feat: add distance motion profile`.

### Task 4: Extend MotorController for controller use

**Files:**
- Modify: `include/motor_controller.h`
- Modify: `src/motor_controller.cpp`

**Interfaces:**
- Preserve `setSpeed(int)` for manual mode.
- Add `setVoltage(float)` to convert +/-12 V to signed +/-255 PWM.
- Add `direction()` returning -1, 0 or +1 from the applied signed command.

- [ ] Add voltage-to-PWM conversion with saturation.
- [ ] Preserve existing CW positive and CCW negative mapping.
- [ ] Keep `stop()` and `setEnabled(false)` semantics unchanged.
- [ ] Build with `pio run`.
- [ ] Commit `feat: support voltage commands in motor controller`.

### Task 5: Implement distance controller

**Files:**
- Create: `include/distance_controller.h`
- Create: `src/distance_controller.cpp`

**Interfaces:**
- Consumes `Encoder`, `MotionProfile`, `MotorController`.
- Produces `beginMove(distanceCm, direction)`, `update()`, `cancel()`, `isActive()`, `status()`, `commandDistanceCm()`, `moveStartPositionCm()`.

- [ ] Implement IDLE, MOVING, SETTLING, COMPLETE and CANCELLED states.
- [ ] Capture encoder position at START and compute relative measured position from that snapshot.
- [ ] Lock encoder direction to the selected move direction for the entire active move.
- [ ] Compute position correction, velocity command, velocity feedback, feedforward and voltage saturation in the Simulink order.
- [ ] Convert final voltage to motor PWM using `MotorController::setVoltage()`.
- [ ] After profile completion, require one-pulse position tolerance plus stopped velocity for consecutive updates before COMPLETE.
- [ ] Prevent opposite-direction correction while the encoder reports motion. Stop first, then allow any correction phase only after stopped confirmation.
- [ ] Implement cancel as zero PWM plus disabled BTS7960.
- [ ] Build with `pio run`.
- [ ] Commit `feat: add relative distance controller`.

### Task 6: Integrate distance control with WifiHotspot

**Files:**
- Modify: `include/wifi_hotspot.h`
- Modify: `src/wifi_hotspot.cpp`

**Interfaces:**
- Add HTTP/WebSocket commands for distance START, STOP, ZERO and status.
- Broadcast distance state including `positionCm` and `distanceStatus`.

- [ ] Make WifiHotspot own Encoder and DistanceController alongside MotorController.
- [ ] Initialize encoder and distance controller in `begin()`.
- [ ] Call encoder/distance updates from `WifiHotspot::update()` at a fixed control cadence without blocking Wi-Fi.
- [ ] Cancel active distance mode before any manual speed/on/off command.
- [ ] Reject ZERO while distance mode is active; otherwise call encoder zero.
- [ ] Implement START validation for positive distance and CW/CCW selection, then enable motor driver and begin move.
- [ ] Implement STOP as distance cancel plus disabled motor driver.
- [ ] Broadcast live position/status over WebSocket.
- [ ] Build with `pio run`.
- [ ] Commit `feat: integrate distance mode with web control`.

### Task 7: Add minimal distance-control UI

**Files:**
- Modify: `data/index.html`
- Modify: `data/script.js`
- Modify: `data/style.css`

**Interfaces:**
- UI sends `{cmd:"distance_start", distanceCm:<number>, direction:"cw"|"ccw"}`, `{cmd:"distance_stop"}`, `{cmd:"distance_zero"}`.
- UI consumes `positionCm` and `distanceStatus` fields.

- [ ] Add distance input in cm, CW/CCW buttons, START, STOP and ZERO buttons.
- [ ] Add current position and simple status text.
- [ ] Keep controls minimal and visually consistent with the existing page.
- [ ] Disable ZERO while status is MOVING/SETTLING.
- [ ] Ensure manual slider behavior remains unchanged except that firmware cancellation happens automatically.
- [ ] Upload/build filesystem image if available and run `pio run` for firmware.
- [ ] Commit `feat: add distance control web interface`.

### Task 8: Verify integrated behaviour

**Files:**
- Review all modified and created files.

**Interfaces:**
- No new interfaces.

- [ ] Run `pio run` and require a successful build.
- [ ] Inspect the branch diff for accidental unrelated changes.
- [ ] Verify the following flows by code review: CW relative move, CCW relative move, STOP cancellation, ZERO when idle, ZERO rejection while moving, manual-command cancellation, continuous position tracking, and stopped-before-reversal logic.
- [ ] Commit any verification fixes with a focused message.
- [ ] Open a pull request from `feature/distance-control` to `main` with a concise technical summary and test evidence.
