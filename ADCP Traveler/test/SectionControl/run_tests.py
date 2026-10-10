"""Compile production control code with hardware/time shims, then execute it."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
if __name__ == "__main__":
    folder = root / ".pio" / "section-host-tests"
    folder.mkdir(parents=True, exist_ok=True)
    binary = folder / "section-tests.exe"
    sources = ["encoder", "motor_controller", "motion_profile", "distance_controller",
               "section_plan", "section_controller"]
    subprocess.run(["C:/msys64/ucrt64/bin/g++.exe", "-std=c++17", "-Wall", "-Wextra",
                    "-Werror", "-static", "-I", str(root / "test/SectionControl"),
                    "-I", str(root / "include"),
                    *[str(root / "src" / (name + ".cpp")) for name in sources],
                    str(root / "test/SectionControl/test_sections.cpp"),
                    "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
