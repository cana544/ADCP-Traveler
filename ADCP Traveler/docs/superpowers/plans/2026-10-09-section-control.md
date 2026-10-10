# Section Control Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task by task.

**Goal:** Add measured scanning and variable return sections to the traveller app.

**Architecture:** Firmware owns planning and execution; browser renders reported state. Reuse Encoder, MotorController and DistanceController. Isolate the planner and workflow from HTTP/WebSocket handling.

**Tech Stack:** ESP32 Arduino C++, PlatformIO, plain HTML/CSS/JavaScript, host g++ and Node tests.

**Spec:** ../specs/2026-10-09-section-control-design.md plus the user-supplied implementation specification.

## Global constraints

- N integer 6..60; fixed B=0.8, R=1.8, edge coefficient 20.
- Minimum generated width 10 cm; reject without clamping.
- Scan ramps from zero to 100% over four seconds; STOP removes output immediately.
- Gauge in the opposite direction, with conceptual numbering LB to RB.
- Only STOPPED / SCANNING / MOVING / FINISHED visible; one primary action.
- No merging, pushing or hardware uploading.

## Review focus

- Browser refresh/reconnect must reflect firmware progress before enabling controls.
- STOP coast must contribute to span and remaining distance.
- Power OFF or another tab must never leave the scan ramp running.
- Invalid configuration must revoke GO eligibility and preserve the model.
- Absolute targets must prevent cumulative drift over 60 moves.

## Task 1: Planner and workflow

Create include/section_plan.h, src/section_plan.cpp, include/section_controller.h and src/section_controller.cpp. Add Config::Section::SCAN_RAMP_DURATION_US. Tests in test/SectionControl compile actual production encoder/motor/distance/planner/controller against a hardware-only Arduino shim.

Interfaces: SectionPlan::generate(float spanCm, int count), maxValidCount(float); SectionController::startScan(int), stop(), configure(double), go(), newScan(), invalidate(), update(), and read-only stage/plan/progress getters. Commands return bool and expose an error message. SectionController consumes references to the existing encoder, motor and distance controller.

- [x] Write failing host tests: N=6 weights produce 1.8x bank widths; all 6..60 distributions sum to span and are symmetric; reject short/nonfinite spans and fractional counts; scan ramp is monotonic and reaches signed 255; stop output is zero and coast changes span; both return orders; interruption resumes remaining absolute target; completion waits for GO; failure does not increment.
- [x] Run python test/SectionControl/run_tests.py and observe missing feature failure.
- [x] Implement planner and workflow against those tests.
- [x] Run host tests and firmware compilation.
- [x] Commit controller milestone.

## Task 2: Transport integration

Modify include/wifi_hotspot.h and src/wifi_hotspot.cpp. Add section_scan, section_stop, section_configure, section_go, section_new_scan and section_status commands and corresponding /section routes. Serialize access to motion state. Include stage, span, count, maximum count, widths, completed/current/next section and remaining distance in the common status JSON. Validate JSON types and HTTP parameters before applying commands.

- [x] Test power interruptions and plan invalidation with production controller; inspect manual-command integration in whole-branch review.
- [x] Implement HTTP/WebSocket integration, clear pending manual reversal when starting section work, and update workflow after distance control each tick.
- [x] Build ESP32 and rerun host tests.
- [x] Commit transport milestone.

## Task 3: Native mobile tab

Modify data/index.html, data/style.css and data/script.js; create data/section-control.js for focused rendering and command handling. Generalize page translation for three tabs. Register new script route and HTTP fallbacks. The DOM uses firmware widths; no second planner in JavaScript.

- [x] Write failing Node rendering tests for staged controls, STOP during motion, invalid N, reconnect gating, section colors, completion and detailed-strip swipe isolation.
- [x] Implement staged tab, proportional overview, detail strip, accessible progress and compact New Scan.
- [x] Run Node tests, host tests, firmware and SPIFFS builds.
- [ ] Inspect rendered narrow and desktop layouts: browser automation unavailable in this session.
- [x] Commit UI milestone.

## Task 4: Review and delivery

- [x] Review entire branch against user acceptance checklist and fix significant findings with regression tests.
- [x] Run final checks and report any limits, including physical bench verification.
- [x] Commit verification notes; leave branch unmerged.
