# Operational deployment — 10 November 2026

Uploaded the production firmware with the new distance controller and the unchanged original app filesystem to COM3 over USB, as requested. Both uploads succeeded and verified flash data.

The controller source matches the version exercised in the unloaded tests. The app files in `data` are unchanged from Git HEAD and include the existing Speed, Distance, and Section Control interface. The disposable serial MOVE/PING test interface is absent. Normal production defaults remain in effect, including adaptation enabled at boot.

Fresh controller, app integration, startup/cancellation, IMU transport, section-control, UI, and battery software checks passed. After upload, the HTTP and WebSocket servers started, and 25 serial diagnostic samples showed zero PWM and constant encoder position. STOP and LOG OFF were sent before closing the USB port. No motion commands were issued and no Wi-Fi connection was made; the live app was not exercised over HTTP.

The IMU remains unhealthy and loaded towing performance remains unvalidated. This deployment does not include the later suggested approach-control or diagnostic improvements.

Archived images: `firmware.bin` and `spiffs.bin`. `manifest.json` records their SHA-256 hashes and source/app file hashes. Upload logs, boot diagnostics, and `verification.json` preserve the deployment evidence.
