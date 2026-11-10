# Unloaded controller results ? 10 November 2026

Generate the full plotted report locally with `python test/LoadedControl/plot_unloaded.py test/LoadedControl/results/unloaded-2026-11-10` for summary graphs, every hardware trace, historical context and seven synthetic changing-load scenarios. Generated HTML/images and firmware binaries are ignored; raw evidence and metrics remain tracked.

## Finding

The unchanged production controller completed all 60 unloaded positioning requests under their original command. All encoder endpoints were in the one-sided 0?1 cm band before the target. This supports the position-based completion and bounded recovery mechanism as a proof of concept. It does **not** establish loaded towing accuracy or uniformly smooth motion.

37 of 60 moves needed automatic recovery. The endpoint shortfall ranged from 0.4690 to 0.9912 cm; no encoder movement or nonzero PWM was observed in the 2.5-second post-completion windows. Targets remained fixed; no commanded reversal, diagnostic drops, sequence gaps, malformed records, or reboot occurred in the matrix. These are sampled encoder observations at approximately 5 Hz.

## Hardware matrix

Adaptation OFF: 10, 20, 30, 50, 100, 250, 500 cm, both directions, three repeats each (42 moves). Adaptation ON after reset: 10, 50, 500 cm, both directions, three repeats each (18 moves). All distances are encoder-equivalent shaft travel; no physical traveller translation was measured.

| Request cm | Trials | Maximum encoder shortfall cm | Duration range s | Recovery moves |
|---|---:|---:|---:|---:|
| 10 | 12 | 0.5752 | 7.49?14.99 | 6 |
| 20 | 6 | 0.5221 | 7.99?16.79 | 2 |
| 30 | 6 | 0.4690 | 9.90?17.79 | 5 |
| 50 | 12 | 0.9912 | 8.89?15.50 | 6 |
| 100 | 6 | 0.7257 | 12.79?18.49 | 3 |
| 250 | 6 | 0.5575 | 25.59?27.70 | 6 |
| 500 | 12 | 0.4868 | 33.70?41.69 | 9 |

Tracking remains imperfect: sampled filtered velocity reached 36.37 cm/s against a maximum 20 cm/s request. The isolated peaks cannot be classified as genuine shaft-speed peaks or measurement artifacts without an independent reference or faster pulse-level capture. Motor output reacted to the estimate. Recovery pauses and variable move times remain visible; successful endpoint screening is not a smoothness certification.

Learning samples and direction-specific learned values were observed in the longer adaptation-on runs. This sequential, small unloaded comparison does not demonstrate a benefit: average 50 cm duration was 11.63 s OFF versus 13.68 s ON, and 500 cm was 37.49 s OFF versus 38.29 s ON. These are descriptive results, not a randomized comparison or a loaded tuning recommendation.

## STOP and heartbeat

All five intentional interruption tests passed: STOP during startup, STOP during early travel, STOP during approach, heartbeat loss after startup, and heartbeat loss during travel. Zero PWM was observed from the first STOPPED sample onward, with no automatic restart and a stationary final one-second window. The travel heartbeat case coasted 1.2566 cm after the first STOPPED sample. This demonstrates why zero motor command is not instantaneous mechanical rest. Exact command-to-output latency was not measured.

## Synthetic dynamics

68 attempts used the real production controller with the existing actuator-responsive, uncalibrated plant, including repeats and both directions/adaptation states. Constant resistance, position-varying resistance, a load step, increased endpoint friction, and a temporary blockage produced 60 COMPLETE outcomes. Permanent blockage produced four NO_PROGRESS faults after two recovery attempts. Sustained assisting load produced four OVERRUN faults and continued model motion after zero output; zero PWM cannot arrest an externally driven plant.

Two COMPLETE attempts nevertheless ended 1.043 cm short in continuous model wheel travel while satisfying encoder acceptance. Relative-move encoder phase/quantisation explains this limitation in the no-slip model. The model has no rope elasticity, traction/slip, boat dynamics, torque/current measurement, or calibrated physical parameters. Its results cannot certify real body displacement, the physical 1 cm requirement, or safety under river load.

## Firmware and remaining limits

Production firmware was restored successfully after testing; the upload verified flash data. The restored binary matches the saved production SHA-256: `f4ffd63ca593aa83a148870a0f3c91049f04773b375c5b9912aaecd2b246120a`. Production source hashes are unchanged. A fresh serial check recorded 26 zero-PWM samples at constant position and rejected TEST PING, confirming the temporary serial interface is absent. STOP and LOG OFF were sent before closing the port.

The IMU still supplies no valid reports; this remains unresolved. No IMU data controlled these moves. Hardware app/Section Control actions, loaded operation, traction, physical endpoint accuracy, wet rope, and towing were not validated. Existing software tests cover section progression, cancellation, load changes, bounded recovery, sensor transport, UI, battery calculations, and capture/report behavior.

All relevant software regression checks and the temporary interface checks passed; the reporting direction/phase checks passed after fixing plotting normalization. An independent read-only review found no important remaining calculation defects.

## Evidence

- [Hardware summary CSV](hardware-summary.csv), [derived tracking/learning metrics](hardware-derived.json), and [raw matrix log](matrix.log).
- [STOP logs](stops.log), [travel/approach STOP logs](stop-phases.log), and corresponding JSON metrics.
- [Simulation traces CSV](simulation.csv) and [simulation summary CSV](simulation-summary.csv).
- [Raw-log verification](evidence-verification.json), [restoration verification](restoration-verification.json), and [upload log](restore-upload.log).
- Source/binary identity manifests and `temporary-interface.patch`; original saved production image is in the preceding 9 November results folder.
- Replot without hardware access: `python test/LoadedControl/plot_unloaded.py test/LoadedControl/results/unloaded-2026-11-10`.
