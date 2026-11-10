"""Compile production automatic-motion code with host GPIO/time shims."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
if __name__ == "__main__":
    out = ROOT / ".pio" / "loaded-host-tests"
    out.mkdir(parents=True, exist_ok=True)
    cpp = "C:/msys64/ucrt64/bin/g++.exe"
    common = [cpp, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-static",
              "-I", str(ROOT / "test/DistanceControlTest/host"), "-I", str(ROOT / "include")]
    for test in ("test_control", "test_imu", "test_commands"):
        exe = out / (test + ".exe")
        subprocess.run([*common, str(ROOT / "test/LoadedControl" / (test + ".cpp")), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=30)
    for test in ("regression", "app_regression", "startup_regression"):
        exe = out / (test + ".exe")
        subprocess.run([*common, str(ROOT / "test/DistanceControlTest/host" / (test + ".cpp")), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=30)
    # Link the actual pinned SH-2 implementation, not a mocked sensor API.
    library = ROOT / ".pio/libdeps/esp32dev/Adafruit BNO08x/src"
    diagnostic = out / "test_diagnostics.exe"
    subprocess.run([*common, "-I", str(library), "-I", str(ROOT / ".pio/libdeps/esp32dev/ArduinoJson/src"),
                    str(ROOT / "test/LoadedControl/test_diagnostics.cpp"), "-o", str(diagnostic)], check=True)
    subprocess.run([str(diagnostic)], check=True, timeout=10)
    objects = []
    for name in ("sh2", "sh2_util", "sh2_SensorValue", "shtp"):
        obj = out / (name + ".o")
        subprocess.run(["C:/msys64/ucrt64/bin/gcc.exe", "-std=c11", "-I", str(library),
                        "-c", str(library / (name + ".c")), "-o", str(obj)], check=True)
        objects.append(str(obj))
    exe = out / "imu_runtime.exe"
    subprocess.run([cpp, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-static",
                    "-I", str(ROOT / "test/LoadedControl/imu_host"), "-I", str(ROOT / "include"),
                    "-I", str(library), str(ROOT / "test/LoadedControl/test_imu_runtime.cpp"),
                    str(ROOT / "src/imu_monitor.cpp"), *objects, "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=10)
