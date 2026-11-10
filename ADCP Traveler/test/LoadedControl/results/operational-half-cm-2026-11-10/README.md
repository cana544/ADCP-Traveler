# Operational endpoint update — true ±0.5 cm

The user explicitly selected a symmetric encoder acceptance band allowing slight target crossing. Production tolerance is now 0.5 cm. A stationary endpoint within ±0.5 cm completes; crossing more than 0.5 cm beyond the target, plus the existing numerical epsilon, faults without commanded reversal. Section Control uses the same band, including GO confirmation after a pause. Fixed targets, stop/settling requirements and effort limits remain intact. The original app files are unchanged.

Boundary tests first failed under the prior policy, then passed under the new policy. Checks cover both directions, exact ±0.5 cm boundaries, ±0.501 cm rejection, coasting across the target during settling, and section acceptance/rejection. Production controller, sensor transport, command/diagnostic, app integration, startup/cancellation, Section Control, and all 19 app UI tests passed. An independent source review found no important issues. The operational firmware built and uploaded successfully over COM3 with flash hash verification.

`verification.json` and `boot-verification.log` record the post-upload boot/idle check. The firmware image and SHA-256 source manifest are archived here. STOP and LOG OFF were sent before the port was closed. No powered moves or Wi-Fi connections were made for this update.

This narrower band has software verification and a boot/idle check, not fresh physical movement validation. The earlier 60-move unloaded evidence used the former one-sided 0–1 cm band. Encoder-based acceptance does not establish physical ±0.5 cm accuracy, particularly with slip or load. The existing IMU data issue remains unresolved.
