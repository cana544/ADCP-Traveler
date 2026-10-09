# Automated unloaded distance-control tuning

## Agreed outcome

Tune the standalone `test/DistanceControlTest/DistanceControlTest.ino` on the
ESP32 connected to COM3. The main shaft and encoder spin freely; the traveller
does not translate. Find one shared gain set that gives smooth equivalent
wheel travel over 10–500 cm. Prioritise smoothness over minimum move time.
These measurements characterise the unloaded shaft, not loaded travel.

The primary objective at every commanded distance in 10–500 cm is to follow
the planned velocity profile as closely as the motor and encoder allow: start
smoothly from rest, execute one continuous move, decelerate smoothly to rest
at the commanded endpoint, and remain stopped. A run with a second corrective
move, stop/restart, hunting around the target, reversal, jitter or repeated
PWM kicks is unacceptable even if its final distance error is small. Endpoint
accuracy and smooth profile tracking must be achieved together; do not improve
one by accepting a failure of the other.

"At the commanded endpoint" means within the agreed one-pulse measurement
tolerance (approximately 0.628 cm) after stopping, with no subsequent movement.
The single-channel encoder cannot demonstrate an infinitely exact position or
resolve movement smaller than one pulse. Report measured error and this
resolution explicitly rather than claim exact physical positioning.

Test distances, in descending order: 500, 250, 100, 75, 50, 30, 25, 20, 10 cm.
Validate both CW and CCW, with three repetitions of each final combination.

## Proposed implementation

Keep this workflow in the standalone test sketch and its host scripts. Replace
the sketch's automatic boot move with an idle serial command interface. Boot,
reset, upload and serial connection must leave PWM at zero. Commands configure
limits and gains, start a relative move, report status, and stop. Configuration
changes are rejected while moving. The host waits for READY, configures a run,
starts it explicitly and captures telemetry through a terminal event.

Use the existing analytical MotionProfile as the independent position and
velocity reference. Start with a common vmax of 20 cm/s and amax of 10 cm/s²
for every distance, replacing the current short-distance limit switch. The
transition distance is vmax²/amax = 40 cm: shorter moves are triangular and
longer moves are trapezoidal. At the boundary the cruise duration is zero.
Verify position, velocity, duration and integrated distance before hardware
tests. Log actual reference separately from the feedback-adjusted command.

Track reference position and velocity using position feedback, velocity PI,
and feedforward. Start with derivative gain zero because encoder pulses are
coarse. Handle motor breakaway and low-speed friction explicitly; do not label
a friction-induced stop/restart as successful tracking. Use actual elapsed
control time, bounded integral accumulation and output limiting. Preserve
encoder sign during coasting; any reversal waits for stationary confirmation.
Final stationary error must be within one pulse, approximately 0.628 cm.

Provide bounded trial duration and loss-of-host timeout in firmware, plus a
STOP command. Each timeout stops PWM and reports an error. The host stops the
current trial on invalid telemetry, missed completion or excessive travel;
never continue tuning a trial that has failed to terminate. No commands
request a move outside 10–500 cm. Allow stationary confirmation between runs.

## Hardware workflow

1. Test firmware and host parsing against synthetic cases and compile for ESP32.
2. Upload command-driven firmware once to COM3 and verify idle/READY.
3. Run 500 cm with the reduced motion limits; capture the baseline.
4. Check profile shape, tracking, actuator effort and completion. Adjust
   feedforward/friction terms and gains using measured traces, one interpretable
   change at a time. Gains are configured at runtime.
5. Work down the distance list. Check long-distance anchors after short-distance
   improvements; retain a candidate only when it improves the range overall.
6. Freeze one common set of limits, feedforward/friction terms and feedback
   gains. Run all nine distances in both directions three times.
7. Save the selected configuration as defaults in the standalone sketch, build,
   upload, and leave the motor idle. Deliver the graphs and result table.

Limit exploration to 60 tuning trials before final validation. If that budget
does not find an acceptable shared set, report the measured best candidate and
the specific failing distances rather than claim success. Do not silently
introduce separate gains or limits for individual distances.

## Assessment and evidence

Record raw serial logs, CSV telemetry, run settings and terminal outcome for
every trial. Include elapsed time, measured relative distance, reference
position, position error, measured velocity, reference velocity, commanded
velocity, voltage effort, signed PWM, controller state and pulse timing.

Generate time-aligned charts of position/reference, velocity/reference/command,
position error, and effort/PWM. Summarise final error, overshoot, tracking RMSE,
peak tracking error, duration, intermediate stops and restart kicks. Analyse
the active move separately from stationary confirmation. Inspect raw pulse
timing as well as filtered speed so smoothing cannot hide physical stops.

Graphs are a required deliverable throughout tuning and at the end. After each
trial, save a labelled four-panel trace containing position/reference, velocity
reference/command/measurement, position error, and voltage/PWM. Annotate the
distance, direction, trial number, gains, motion limits, profile type and
terminal outcome. Show individual encoder pulse intervals where they help
explain low-speed dips. Keep voltage and PWM on separate labelled scales.

Maintain a tuning-progress dashboard showing tracking RMSE, final error,
overshoot and intermediate stop/restart counts against trial number. Mark
parameter changes and separate distances/directions so an easier test cannot
make a worse candidate appear better. Compare candidate settings on matching
distance/direction trials.

For the frozen shared configuration, generate a final dashboard with metrics
against distance: signed final position error, maximum overshoot, normalised
velocity tracking RMSE, peak velocity error, move duration and stop/restart
counts. Display both directions and all three repeats, with mean and range.
Draw the one-pulse position tolerance and proposed tracking screening target.
Also provide a grid of actual/reference velocity profiles across all nine
distances, plus baseline-versus-final traces at 500, 100 and 10 cm where both
measurements are available. Collect these baseline anchor trials before changing
the initial gain set, using the reduced common motion limits. Save standalone
PNG graphs and a browsable HTML report linking the plots, settings, raw CSVs
and result table. Distinguish measured results from reference-only calculations.

Accepted runs have the correct analytical profile, final stationary error no
greater than one encoder pulse, one continuous acceleration/deceleration cycle,
smooth startup and stopping, no intermediate stop/restart, no second corrective
move, no target hunting or reversal, no jitter or repeated drive kicks, and no
sustained oscillation. Keep recording for at least one second after completion
to confirm the shaft remains stopped with PWM zero. Use velocity RMSE
of 15% of reference peak as an initial numerical screening target; it is a
proposed screening measure, not a previously agreed hard acceptance limit.
Review the plots alongside these measures, especially for 10–30 cm where
encoder quantisation is significant. Report any unsatisfied criterion.
Apply these criteria to every validation repetition at every tested distance
in both directions. A good average cannot hide a failed individual move.
Validation at sampled distances supports the intended 10–500 cm operating
range but is not proof of performance at every untested distance.

## Deliverables and limits

Deliver the command-driven standalone sketch, reusable serial capture/analysis
scripts, repeatable tests, raw trial data, per-trial plots, a tuning-progress
dashboard, final metric and velocity-profile dashboards, a browsable report,
a comparison table, and the chosen shared configuration with its demonstrated
passing distance range.
Changes to the main web-connected application are outside this tuning pass.

COM3 is confirmed by the user and listed by the host. Serial access, upload and
hardware response have not yet been tested. Port contention or disconnection
may require the user to reconnect the board or close another serial monitor.
The encoder has one channel: actual rotation direction is inferred from the
command, so the test must not command reversal while coasting.

## Alternatives considered

Rebuilding and uploading a fixed-distance sketch for every trial avoids a
command interface but makes tuning slow and resets the controller every time.
Runtime serial configuration is the selected approach because it supports
repeatable trials and preserves explicit control of when motion starts.
Per-distance gain scheduling could improve individual cases but does not meet
the agreed objective of one shared gain set.
