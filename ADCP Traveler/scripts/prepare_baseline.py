"""Prepare an isolated logged baseline without changing its distance controller."""
import argparse
import io
import subprocess
import zipfile
from pathlib import Path

BASE = "5b1e4c310e9129574d01ef064bc92a3ca8219cb1"
REPO = Path(__file__).resolve().parents[2]
PROJECT = Path(__file__).resolve().parents[1]

def prepare(destination):
    destination = Path(destination).resolve()
    destination.mkdir(parents=True, exist_ok=False)
    archive = subprocess.check_output(["git", "archive", "--format=zip", BASE], cwd=REPO)
    with zipfile.ZipFile(io.BytesIO(archive)) as bundle:
        prefix = "ADCP Traveler/"
        for item in bundle.infolist():
            if item.is_dir() or not item.filename.startswith(prefix): continue
            relative = item.filename[len(prefix):]
            if not (relative.startswith(("src/", "include/", "data/", "lib/")) or relative == "platformio.ini"): continue
            output = (destination / relative).resolve()
            if not output.is_relative_to(destination): raise ValueError("archive path escapes destination")
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(bundle.read(item))
    # Archive export attributes can convert line endings. Preserve the exact
    # committed controller blob so its baseline hash is independently checkable.
    controller = subprocess.check_output(["git", "show", BASE + ":ADCP Traveler/src/distance_controller.cpp"], cwd=REPO)
    (destination / "src/distance_controller.cpp").write_bytes(controller)
    # Read-only accessors for logging; original controller implementation untouched.
    header = destination / "include/distance_controller.h"
    text = header.read_text(encoding="utf-8")
    anchor = "float moveStartPositionCm() const;"
    assert text.count(anchor) == 1
    header.write_text(text.replace(anchor, anchor + "\n  float diagnosticTargetCm() const { return targetPositionCm_; }\n"), encoding="utf-8")
    (destination / "include/diagnostic_commands.h").write_bytes((PROJECT / "include/diagnostic_commands.h").read_bytes())
    source = destination / "src/wifi_hotspot.cpp"
    text = source.read_text(encoding="utf-8")
    text = '#include "diagnostic_commands.h"\n' + text
    anchor = "  batteryMonitor_.update();"
    assert text.count(anchor) == 1
    instrumentation = (PROJECT / "scripts/baseline_diagnostics.inc").read_text(encoding="utf-8")
    source.write_text(text.replace(anchor, anchor + "\n" + instrumentation), encoding="utf-8")
    main = destination / "src/main.cpp"
    text = main.read_text(encoding="utf-8").replace("  Serial.begin(115200);", "  Serial.setTxBufferSize(4096);\n  Serial.begin(115200);")
    main.write_text(text, encoding="utf-8")
    (destination / "BASELINE.txt").write_text("Source " + BASE + "\nRead-only USB logging and STOP added; distance_controller.cpp unchanged.\n", encoding="utf-8")
    return destination

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=str(PROJECT / ".pio/loaded-motion/baseline-source"))
    args = parser.parse_args()
    print(prepare(args.output))
