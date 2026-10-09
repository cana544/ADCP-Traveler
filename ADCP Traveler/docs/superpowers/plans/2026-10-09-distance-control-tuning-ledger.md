# Tuning execution record

- User approved COM3, unloaded shaft, 10–500 cm, two directions, one shared configuration and final graphs; explicitly instructed implementation/build after clarifications.
- Execution is in the current workspace and test folder. No main-app changes or commits are required for this deliverable.
- Baseline C++ and existing UI checks passed before implementation.
- Firmware implemented with independent analytical profiles, common reduced limits, runtime settings, idle boot, explicit MOVE/STOP and deadline/heartbeat shutdown.
- C++ regressions failed for old profile limits/automatic boot, then passed after implementation.
- Python capture/metrics/report tests failed before their corresponding implementation and review fixes; current suite has nine passing tests.
- COM3 initially busy: user closed the other serial program; upload then succeeded.
- Hardware endpoint failures and excess-travel abort were saved, stopped, and followed by successful new trials.
- Encoder investigation: 7–8 ms extra edges inflated counts and reciprocal-speed averages. Tested 15 ms floor and averaging periods before reciprocation. Later isolated extra edges on short reverse moves required a tested continuity rule restricted to periods <=150 ms.
- Two-period averaging improved deceleration estimate latency relative to the initial four-period window; a three-period median is the final candidate because it suppresses isolated intervals while preserving plausible pulse counts.
- Independent review found post-completion capture missing, unavailable RMSE serialisation failing, and malformed timestamps ignored. All reproduced in tests and corrected; reviewer confirmed no remaining Important findings.
- Report comparison uses one frozen settings group and labels incomplete validation. Failed/missing-metric runs remain in the table.
- Ruling: proposed 15% normalised velocity RMSE remains a screening target, not a retroactively imposed physical accuracy guarantee. Numerical results and plots must both be reported, including encoder-limited startup steps.
- Exploration count before full range sweep: 41 trials. Sweep adds 18.
- Hardware regression: the relative pulse-continuity rejection rule locked out valid pulses in a 500 cm reverse trial (97.25 cm endpoint shortfall). The trial stopped, was saved as failure, and subsequent trials recovered. A failing C++ abrupt-speed-change count test reproduced the lockout. Removed relative rejection entirely; retain only the fixed 15 ms debounce, and smooth velocity via a three-period median.
- Ruling: use trial 60 to recheck the failing 500 cm reverse anchor instead of a deliberate hardware heartbeat-loss test. Deadline/heartbeat behaviour is already covered by controller tests; hardware endpoint failure and subsequent recovery have been demonstrated. Do not add unbounded tuning trials beyond this cap.
- Trial 60: 500 cm CCW passes after median/count-lockout fix; final error -0.4868 cm, normalised velocity RMSE 3.34%, zero detected intermediate stops/restarts, >1.1 seconds post-completion stationary capture.
- Frozen configuration: vmax=20 cm/s, amax=10 cm/s², position Kp=3, velocity Kp=13, velocity Ki=2, feedforward=23 V/(m/s), friction=1.8 V. Defaults saved, compiled and uploaded before validation; source stays frozen during all 54 validation runs.
- Final validation started: nine distances, both directions, three repeats each. Per-trial PNGs are generated immediately; aggregate validation dashboards refresh after each completed distance group to avoid unnecessary plotting delays.

- Final validation complete: 54 runs (9 distances x 2 directions x 3 repeats), one firmware source hash and one shared settings set. All 54 COMPLETE; all endpoints within 0.4868 cm; zero detected intermediate stops, restart kicks or post-completion encoder movement.
- Numerical velocity screening: 48/54 pass. All six 10 cm trials exceed the 15% screening target (15.10-18.02%); 20-500 cm all pass. Do not describe 10 cm tracking as fully smooth or all criteria met.
- Final graph review: analytical triangles/trapezoids appear correct; actual short profiles retain startup/encoder steps and deceleration lag. Isolated speed-estimate spikes remain on some longer runs. No claim of sub-pulse accuracy or loaded performance.
- Final verification: real-sketch C++ regression checks, nine Python tests, existing UI layout check and git diff whitespace check pass. Selected firmware was built/uploaded before validation. After validation COM3 explicitly acknowledged STOP and returned DC_STATUS,IDLE,0.
- Final report regenerated with all 114 trials, individual plots/raw logs and explicit screening failure reasons. Results README records measured limits.
