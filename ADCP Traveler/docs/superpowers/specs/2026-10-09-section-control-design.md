# Section Control design

The supplied Section Control specification is the authority for this change. Add a third native tab to Gauge Glide for a measured scan followed by individually triggered return sections. Keep the existing branding, power/status cards and navigation. Use the concept image for appearance; omit its draggable boundaries, Mode, Save Sections, Auto Split and Reset controls.

## Confirmed scan behavior

The user confirmed 100% scan speed with gradual acceleration. Ramp PWM smoothly from zero to 255 over four seconds (a fixed firmware constant), in the selected LB/RB direction. STOP immediately removes motor output. Measure encoder displacement, including coasting after STOP until the wheel is stationary. Return gauging uses the existing distance controller and its acceleration settings.

## Firmware and control

Implement a pure fixed-capacity SectionPlan, with the exact weighting equation, percentages, boundaries, midpoints and physical widths in cm. Reject nonintegral counts, counts outside 6..60, nonpositive spans and widths below 10 cm. Determine maximum valid N by checking all counts through 60 without modifying the model.

SectionController owns PRE_SCAN, SCANNING, CONFIGURE, READY_FOR_SECTION, MOVING_SECTION and FINISHED. Expose only STOPPED, SCANNING, MOVING and FINISHED as traveller states. Own the state in firmware and publish it through existing status messages. Scan RB executes N..1 on return; scan LB executes 1..N. Use absolute endpoints anchored at the final scan position, so rounding and interrupted moves do not accumulate errors. STOP never increments the completed count. GO resumes toward the same endpoint after the encoder confirms stationary. Advance only on a successful distance-controller completion, or an explicit GO confirming an interrupted endpoint within encoder tolerance. A failed move stays recoverable without skipping its section; an unrecoverable overshoot requires a new scan.

Changing N is allowed only before any gauging movement begins. New Scan clears a stationary workflow. Manual speed, independent distance motion or encoder zero invalidates a stopped section plan, since these change its reference. Power OFF stops and preserves a section interruption. All commands validate in firmware, not just the browser. Serialize control access between the network callbacks and control loop.

## UI

One primary RUN / STOP / GO button, a pre-scan LB/RB selector, a measured span/count/next-distance summary, and configuration revealed after scanning. Number of sections is the only model input. Show the fixed 80% normalized-index threshold and 1.8x ratio. Use proportional red-to-violet widths in a compact overview and an independently scrollable labeled detail strip. Highlight completed and next sections without implying measured flow. Keep bank numbering left-to-right. Present scan/return direction, completed section and next section; keep New Scan as a small secondary text action. Disable commands until fresh state is received after reconnect. Poll status for HTTP fallback.

## Verification

Compile and execute production C++ on the host with only hardware/time stubs, testing the mathematical model, direction ordering, ramp, immediate STOP, coasting, interrupted resume, completion, invalid counts and controller failure. Test browser state rendering and command selection with Node and inspect mobile layouts. Build ESP32 firmware and SPIFFS image. Physical movement still requires bench verification with the actual traveller. Commit each meaningful milestone on feature/section-control; do not merge or push.
