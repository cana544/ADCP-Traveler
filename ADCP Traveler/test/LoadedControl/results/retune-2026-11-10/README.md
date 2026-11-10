# Continuous distance controller retuning ? 10 November 2026

The reviewed operational firmware is uploaded, the original app is unchanged, and the motor was left stopped. See the tracked [summary](plots/comparison.png), [50 cm RB comparison](plots/50cm-RB.png) and [50 cm LB comparison](plots/50cm-LB.png).

Generate the full HTML report and all per-trial plots locally with `python test/LoadedControl/plot_retuning.py test/LoadedControl/results/retune-2026-11-10`. Generated HTML, additional images and firmware binaries are ignored; the source measurements and settings below remain tracked.

## Outcome

All **56 locked-settings validation moves passed** across sampled requests of 5, 10, 20, 25, 30, 50, 75, 100, 250 and 500 cm, both directions. These include adaptation OFF and ON. Every endpoint was within the symmetric ?0.5 cm **encoder** band, with zero automatic recoveries, no sampled interior pulse age reaching one second, and no observed encoder movement or nonzero PWM during 2.5 seconds after COMPLETE.

The pre-retuning baseline completed its eight requests but used seven recoveries. Its maximum sampled pulse age outside the band was 7.469 s, compared with 0.680 s in the final validation. Matched 10/20/50/100 cm direction-balanced mean duration fell from 16.81 s to 8.59 s. The data are sequential single-rig measurements, not randomized trials; they do not prove a globally optimal gain set.

## What changed

| Setting | Baseline | Selected common setting |
|---|---:|---:|
| Velocity Kp | 13 | 13 |
| Velocity Ki | 2 | 4 |
| Feedforward nominal V/(m/s) | 23 | 17.5 |
| Friction compensation nominal V | 1.8 | 2.3 |
| Approach request within 8 cm | 2 cm/s | 3 cm/s |

The earlier braking room was `remaining ? 0.5*tolerance ? margin ? speed*reaction`. With a 0.5 cm tolerance, it could request zero normal speed while still outside the accepted band. It now plans to the far permitted edge: `remaining + tolerance ? margin ? speed*reaction`. The original absolute target never moves. The 0.3 cm margin and 0.25 s reaction allowance remain; the available planning distance increases by 0.75 cm relative to the former expression. Settling, stationarity and ?0.5 cm overrun checks remain authoritative.

The same PI/feedforward settings apply across the range and both directions. A distance-dependent gain schedule was not needed in these unloaded tests. Startup breakaway learning, running-load learning, bounded recovery, STOP, no commanded reversal, and existing effort/time limits are retained. Startup effort is not forced as a hard minimum during deceleration, because breakaway effort may exceed sustaining effort and could fight stopping protection.

## Hardware validation

Adaptation OFF: nine distances from 10?500 cm, both directions, two repeats each (36). Adaptation ON: 10, 50 and 500 cm, both directions, two repeats each (12). The app minimum of 5 cm was additionally tested twice per direction with adaptation OFF and ON (8). Settings were frozen before this validation.

| Request cm | Validation moves | Maximum absolute encoder error cm | Duration range s | Recoveries |
|---|---:|---:|---:|---:|
| 5 | 8 | 0.0265 | 5.00?5.50 | 0 |
| 10 | 8 | 0.0531 | 6.50?6.99 | 0 |
| 20 | 4 | 0.1062 | 7.19?7.69 | 0 |
| 25 | 4 | 0.4956 | 6.99?7.19 | 0 |
| 30 | 4 | 0.4690 | 7.30?7.59 | 0 |
| 50 | 8 | 0.3628 | 8.79?9.09 | 0 |
| 75 | 4 | 0.2301 | 9.89?10.30 | 0 |
| 100 | 4 | 0.0974 | 11.29?11.59 | 0 |
| 250 | 4 | 0.0708 | 18.99?19.50 | 0 |
| 500 | 8 | 0.4868 | 31.29?31.59 | 0 |

The signed final encoder error ranged from -0.4956 to +0.1062 cm. Positive means beyond the nominal target. All are encoder-equivalent shaft travel; actual traveller/body displacement was not measured.

Continuity screening means: COMPLETE within ?0.5 cm; zero recoveries; sampled pulse age outside the band below one second after startup; stationary zero-output post-completion window. At the 0.628 cm encoder resolution and approximately 5 Hz logging, this does not establish perfectly uninterrupted sub-pulse motion or exact mechanical speed. Isolated filtered-speed spikes remain (maximum 24.61 cm/s in final validation against a 20 cm/s maximum request); no encoder counts were clipped to hide them.

Five STOP/heartbeat checks passed: STOP during startup, travel and approach, plus heartbeat loss after startup and during travel. Output was zero from the first STOPPED sample onward and no restart occurred. One heartbeat case coasted another 0.6283 cm after that sample before becoming stationary. Command-to-zero-output latency was not independently measured.

## Candidate comparison and simulations

Baseline and each of three hardware candidates ran eight comparable moves. A retained the original PI/feedforward but used the new braking geometry and 3 cm/s approach. B used friction=2.3 and feedforward=17.5 with Ki=2. C increased Ki modestly to 4. A, B and C each completed all eight without recoveries. Mean sampled PI-travel speed RMSE was approximately 1.50, 1.14 and 1.12 cm/s respectively; the B/C difference is small, not evidence of a statistically superior optimum. C was selected as a modest correction-speed improvement with successful continuity tests, then validated unchanged.

384 uncalibrated constant-load/distance simulation cases compared 24 parameter/planner combinations. The corrected braking geometry mattered more than simply raising Ki. Larger Ki=8?12 and reduced Kp were also examined; their uncalibrated tradeoffs did not justify deploying them. Additional Kp summaries are retained in `simulation-sweep/extra-*.json`.

Baseline and C each have 68 seven-scenario trace attempts covering both signs/adaptation states: 60 COMPLETE, four NO_PROGRESS faults for permanent blockage, and four OVERRUN faults under sustained assisting load. Abrupt load changes or endpoint friction can still require recovery in the model. The assisting-load model continues moving after zero output. These failures are preserved, not relabelled as successful smooth moves.

The model is actuator responsive but is not calibrated to this motor: it has no rope geometry, elasticity, traction/slip, boat drag, measured torque/current or independent body-position sensor. It does not reproduce all hardware speed tracking. Encoder acceptance can differ from continuous model wheel position. Therefore neither simulations nor unloaded shaft results establish loaded physical ?0.5 cm accuracy or smoothness under changing river loads.

## Verification and deployment

New regression cases first failed against the baseline: positive approach effort just outside the band and continuous nominal synthetic moves. They pass under the chosen controller, including 32 distance/load/sign combinations. Existing controller, startup/cancellation, diagnostics, real IMU transport, section integration, 19 UI, capture/report, frozen standalone tuning and battery checks passed. Independent review compiled the production controller tests and found no important issues.

Only `include/config.h` and the braking-room calculation/comments in `src/distance_controller.cpp` changed in production relative to the preserved baseline. The operational firmware upload verified flash data. HTTP/WebSocket startup and 20 zero-PWM idle samples were verified over USB. TEST PING was rejected, confirming the temporary movement interface was removed. Adaptation remains enabled at normal boot. STOP and LOG OFF were sent before disconnecting. No Wi-Fi connection was made.

The IMU still returns no valid reports; it was not used in control. Live browser/Section Control hardware actions were not exercised in this USB test series, although their shared controller and integration tests passed.

## Evidence and reproduction

- [All measured metrics CSV](all-metrics.csv), [JSON](all-metrics.json), [raw-log consistency verification](evidence-verification.json).
- Stage folders retain raw `trials.log`, measured `trials.json`, settings and firmware identity; all candidates and baseline are preserved.
- `simulation-sweep` retains the 384-case table, summaries, model source, baseline/C scenario traces and plots.
- [Operational source/image manifest](operational/manifest.json), [production patch](operational/retuning.patch), [boot verification](operational/verification.json).
- Replot without hardware access: `python test/LoadedControl/plot_retuning.py test/LoadedControl/results/retune-2026-11-10`.
- Test helper: `retune_capture.py` uses the disposable serial test interface and a two-second heartbeat. It must not be run expecting production firmware to accept TEST MOVE.
