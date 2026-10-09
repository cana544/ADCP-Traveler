# Measured tuning results ? 9 October 2026

The standalone DistanceControlTest firmware was built and uploaded to COM3. Testing used the unloaded shaft; centimetres are encoder-equivalent travel, not measured traveller translation.

60 exploration trials were followed by 54 frozen-configuration validation trials: 500, 250, 100, 75, 50, 30, 25, 20 and 10 cm, both directions, three repeats each. All raw logs, failures and graphs are retained. All validation runs use the same source hash and configuration.

## Selected common configuration

- Maximum velocity: 20 cm/s
- Maximum acceleration: 10 cm/s?
- Position Kp: 3; velocity Kp: 13; velocity Ki: 2; derivative: 0
- Feedforward: 23 V/(m/s); friction compensation: 1.8 V

The analytical profile is triangular below 40 cm and trapezoidal above 40 cm. At 40 cm it reaches maximum speed with zero cruise time. Controller shutdown is bounded by move and heartbeat deadlines; a failed trial is saved and a later explicit move can run. The controller does not perform a reverse correction move.

## Validation evidence

All 54 moves completed. Maximum absolute endpoint error was 0.4868 cm, below one encoder pulse (0.6283 cm). No intermediate stop, restart kick or post-completion encoder movement was detected; each run includes approximately 1.15 seconds after completion.

| Distance cm | Runs | Maximum absolute endpoint error cm | Velocity RMSE / reference peak % |
|---:|---:|---:|---:|
| 10 | 6 | 0.0531 | 15.10?18.02 |
| 20 | 6 | 0.1062 | 10.33?11.85 |
| 25 | 6 | 0.1327 | 9.06?10.34 |
| 30 | 6 | 0.1593 | 8.33?9.62 |
| 50 | 6 | 0.2655 | 6.69?8.64 |
| 75 | 6 | 0.3982 | 5.51?6.49 |
| 100 | 6 | 0.0974 | 4.95?5.78 |
| 250 | 6 | 0.0708 | 4.10?5.31 |
| 500 | 6 | 0.4868 | 2.84?5.14 |

48/54 runs pass the proposed 15% velocity screening target. All six 10 cm runs exceed it: positioning and single-move stopping work, but short-move velocity tracking is not fully smooth. Startup measurement steps, deceleration lag and occasional isolated velocity-estimate spikes remain visible. Encoder quantisation prevents proof of exact sub-pulse positioning or absence of sub-pulse jitter. Loaded performance has not been tested.

## Graphs and data

Open [the full report](index.html) for all graphs, metrics, settings and raw logs. [Velocity profiles](final_velocity_profiles.png) overlay all repeats and references. [Key metrics](final_metrics.png) show means and observed ranges. [Baseline comparison](baseline_vs_final.png) and [tuning progress](tuning_progress.png) show the measured changes. [Summary CSV](summary.csv) includes failed screening reasons.

Verification passed: C++ tests compiling the real sketch, nine Python capture/report tests and the existing UI layout check. Final serial check returned `DC_ACK,STOP` and `DC_STATUS,IDLE,0`.
