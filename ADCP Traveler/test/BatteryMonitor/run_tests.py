from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[2]
binary = root / '.pio/battery-tests.exe'
binary.parent.mkdir(exist_ok=True)
subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-static',
    '-I', str(root / 'test/BatteryMonitor'), '-I', str(root / 'include'),
    str(root / 'test/BatteryMonitor/test_monitor.cpp'), str(root / 'src/battery_monitor.cpp'),
    '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
print('Battery production-code calculation, interpolation, filtering, invalidity and timer wrap tests passed.')
