# Distance Control Tuning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Autonomously tune the unloaded COM3 shaft over 10–500 cm and deliver measured graphs and one shared configuration.

**Architecture:** A command-driven standalone ESP32 sketch produces an analytical motion reference and telemetry. Python captures bounded trials, computes metrics and plots, while the agent selects gain changes from measured outcomes.

**Tech Stack:** ESP32 Arduino, PlatformIO, C++ host regression harness, Python serial/numpy/matplotlib.

**Spec:** `docs/superpowers/specs/2026-10-09-distance-control-tuning-design.md`

## Global Constraints

- COM3, unloaded shaft, commanded distances 10–500 cm.
- Initial common limits 20 cm/s and 10 cm/s²; one common final configuration.
- Final error at most one encoder pulse; no second correction move or restart.
- Preserve raw data, graphs and failed trials; cap exploration at 60 trials.
- Firmware starts idle and stops on trial deadline or lost heartbeat.

## Review Focus

- Nonfinite or malformed command parameters must never start motion.
- Port loss must stop PWM via the firmware heartbeat deadline.
- A stalled move must time out and allow a later explicit trial.
- Encoder quantisation must not hide stop/restart or claim exact positioning.
- Completion must leave PWM zero and record at least one second of stopped data.

### Task 1: Firmware and behavioural verification

**Files:** Modify `test/DistanceControlTest/DistanceControlTest.ino`, host Arduino stub and `host/regression.cpp`.
**Interfaces:** `MotionProfile.start(distance, direction)`, `sample(seconds)`, controller `beginMove`, `update`, `stop`; serial CONFIG/MOVE/STOP/PING/STATUS.

- [x] Write and run failing tests for true reference profiles at 10/40/50/500 cm, idle boot, no-pulse timeout, cancellation and invalid commands.
- [x] Implement independent analytical reference, runtime configuration, tracking controller, explicit terminal events and per-trial deadlines.
- [x] Verify C++ regressions and existing UI check; build standalone firmware for ESP32.

### Task 2: Capture, metrics and graphs

**Files:** Create `test/DistanceControlTest/scripts/tuning.py`, `scripts/test_tuning.py`; update README.
**Interfaces:** `run_trial(port, distance, direction, settings, output)` saves raw log, CSV, JSON and PNG; `analyse(rows, distance, direction)` returns metrics; `report(root)` regenerates dashboards and HTML.

- [x] Write and run failing tests for profile timing, metric calculations, failure persistence and nonfinite data rejection.
- [x] Implement serial READY handshake, heartbeat, configuration, bounded capture, terminal parsing and STOP on host errors.
- [x] Implement per-trial graphs, tuning-progress graph, final metrics/profile grids and browsable report.
- [x] Run Python tests and compile checks before connecting hardware.

### Task 3: Hardware tuning and final evidence

**Files:** Trial artefacts under `test/DistanceControlTest/results/`; selected settings in sketch and README.

- [x] Upload once, verify idle READY and record baseline anchors at 500/100/10 cm using reduced limits.
- [x] Tune descending distances, retain all outcomes and revisit anchors after changes. Examine plots and metrics; do not use a score alone as acceptance.
- [x] Freeze best shared configuration; validate nine distances, two directions, three repeats each.
- [x] Rebuild/upload selected defaults, leave shaft stopped, produce final report including limitations and any failed criteria.

Execution is inline in this session as authorised by the user's instruction to build; no separate worktree or agents are required for this test-folder workflow.
