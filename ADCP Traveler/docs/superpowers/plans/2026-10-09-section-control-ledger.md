# Section Control execution record

Branch: `feature/section-control`, forked from main `ba4e332`. User requested commits and no merge; no push or hardware upload performed.

## Decisions

- User confirmed scan at 100% with gradual acceleration. Fixed four-second smoothstep PWM ramp; STOP cuts motor output immediately. This is a firmware setting, not another user input.
- Use the text specification for behavior and the concept image for style. Its drag handles and extra action buttons are intentionally omitted as required by the text.
- Work on the requested new branch in the existing checkout. Git metadata lives in the parent directory, so branch and commit operations required sandbox escalation.
- Firmware owns the plan and progress. Existing DistanceController is reused without changing its tuning. Absolute section endpoints prevent cumulative encoder-rounding drift.
- Power OFF and STOP preserve interruption; manual motion or zero invalidates the plan because the coordinate reference changes.
- A stationary endpoint interrupted within the existing one-pulse tolerance is confirmed by the next explicit GO, never by STOP itself.
- Whole-branch review was delegated using the requesting-code-review skill. Implementation was performed inline.

## Milestones

- `ecb2ecf`: planner, ramped scan workflow, hardware-shim host tests and design/plan.
- `be0aebb`: HTTP/WebSocket section API, common state snapshots, and serialized control access.
- `99bdf6e`: third mobile tab, proportional width visualization, staged actions and Node tests.
- Final fix/verification commit: reboot-response retirement, request-generation protection, deferred count editing, single-transport STOP, full 60-section execution test and verification notes.

## Test evidence

The new C++ test executable compiles actual production motion code with `-Wall -Wextra -Werror`, substituting only hardware/time calls. Six grouped scenarios cover all section counts, exact fixture widths, minimum width, signed ramp, timer wrap, encoder coast, both return directions, interruption and resume, failures, invalidation, power interruption, and 60 independent moves returning to origin within existing tolerance.

Fourteen Node tests exercise production rendering and the application script. They cover pre-scan and configuration gating, dynamic action labels, proportional widths, colors by size, finished state, stale snapshots, reboot ordering, deferred offline edits, delayed GO/STOP, third-tab isolation, and scrolling without page swipes. HTML parsing also confirmed balanced elements, unique IDs and three pages/navigation buttons.

The existing distance-control suite passes all nine tests. Initially missing serial/numpy imports were resolved by running the existing `.venv` interpreter with the installed PlatformIO site-packages in PYTHONPATH. Python-created temporary directories required running this suite outside the restricted sandbox. No package installation or serial hardware access was needed.

ESP32 firmware and SPIFFS image build successfully. Firmware uses approximately 14.0% RAM and 67.4% flash. The initial full rebuild reported pre-existing AsyncTCP/ESPAsyncWebServer deprecation warnings; final incremental build reports no project compile failures.

## Review findings and fixes

Independent review found no confirmed critical firmware issue. Both important findings were fixed with failing-then-passing tests: stale pre-reboot responses cannot overwrite a new boot, and live WebSocket STOP no longer queues a duplicate HTTP STOP that could affect later GO. The minor deferred-count editing issue was also reproduced and fixed. A further first-seen reboot regression guards HTTP requests that began before the current boot was observed. A timer-wrap regression protects a completed scan ramp from restarting after micros() wraps.

## Verification limits

The CUA tool reports no available browser; rendered phone/desktop layouts have not been visually inspected. Physical scan speed, encoder accuracy at full PWM, coasting and real endpoint accuracy require bench testing. The software is built and committed for that validation, without claiming those physical checks have passed. See test/SectionControl/README.md for commands and the physical checklist.
