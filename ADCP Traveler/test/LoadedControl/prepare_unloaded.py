"""Prepare a disposable USB-only test interface; production sources are untouched."""
from pathlib import Path
import shutil
import json
import hashlib
import difflib
import sys

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / '.pio/unloaded-serial-test'
RESULTS = ROOT / 'test/LoadedControl/results/unloaded-2026-11-09'

def prepare():
    PROJECT.mkdir(parents=True, exist_ok=False)
    RESULTS.mkdir(parents=True, exist_ok=True)
    for folder in ('src', 'include'):
        shutil.copytree(ROOT / folder, PROJECT / folder)
    shutil.copy2(ROOT / 'platformio.ini', PROJECT / 'platformio.ini')
    shutil.copy2(ROOT / '.pio/build/esp32dev/firmware.bin', RESULTS / 'production-firmware.bin')
    # Reuse installed dependencies without downloading or modifying their versions.
    (PROJECT / '.pio').mkdir()
    shutil.copytree(ROOT / '.pio/libdeps', PROJECT / '.pio/libdeps')
    files = list((ROOT/'src').glob('*')) + list((ROOT/'include').glob('*'))
    (RESULTS/'production-source-hashes.json').write_text(json.dumps({str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files if p.is_file()},indent=2))

def extend():
    path = PROJECT/'include/diagnostic_commands.h'
    text = path.read_text().replace('#include <cstring>', '#include <cstring>\n#include <cstdio>\n#include <cmath>')
    text = text.replace('ADAPT_START_ON, ADAPT_START_OFF, ADAPT_LOAD_ON, ADAPT_LOAD_OFF };',
                        'ADAPT_START_ON, ADAPT_START_OFF, ADAPT_LOAD_ON, ADAPT_LOAD_OFF, TEST_MOVE, TEST_PING };\n  float testDistance = 0;\n  int testDirection = 0;')
    text = text.replace('if (!std::strcmp(line_, "STOP")) command = STOP;', '''float distance = 0; int direction = 0; char extra = 0;
      if (!std::strcmp(line_, "TEST PING")) command = TEST_PING;
      else if (std::sscanf(line_, "TEST MOVE %f %d %c", &distance, &direction, &extra) == 2 &&
               std::isfinite(distance) && distance > 0 && distance <= 500 && (direction == 1 || direction == -1)) {
        testDistance = distance; testDirection = direction; command = TEST_MOVE;
      }
      else if (!std::strcmp(line_, "STOP")) command = STOP;''')
    path.write_text(text)
    path = PROJECT/'include/control_diagnostics.h'
    path.write_text(path.read_text().replace('DiagnosticCommands parser_;', 'DiagnosticCommands parser_;\n  uint32_t testHeartbeatUs_ = 0;'))
    path = PROJECT/'src/control_diagnostics.cpp'
    text = path.read_text().replace('  for (int budget', '''  if (controller.isActive() && uint32_t(micros() - testHeartbeatUs_) >= 2000000UL) {
    acknowledge("GG_ACK,TEST_HEARTBEAT_STOP\\n"); return true;
  }
  for (int budget''')
    text = text.replace('      case DiagnosticCommands::NONE: break;', '''      case DiagnosticCommands::NONE: break;
      case DiagnosticCommands::TEST_PING: testHeartbeatUs_ = micros(); break;
      case DiagnosticCommands::TEST_MOVE:
        if (controller.isActive()) acknowledge("GG_ACK,BUSY\\n");
        else {
          controller.beginMove(parser_.testDistance, parser_.testDirection);
          if (controller.isActive()) { testHeartbeatUs_ = micros(); acknowledge("GG_ACK,TEST_MOVE\\n"); }
          else acknowledge("GG_ACK,TEST_MOVE_REJECTED\\n");
        }
        break;''')
    path.write_text(text)
    diff = []
    for folder in ('src','include'):
        for original in (ROOT/folder).glob('*'):
            copied = PROJECT/folder/original.name
            if original.is_file():
                diff.extend(difflib.unified_diff(original.read_text().splitlines(True),copied.read_text().splitlines(True),fromfile=str(original.relative_to(ROOT)),tofile='temporary/'+str(original.relative_to(ROOT))))
    (RESULTS/'temporary-interface.patch').write_text(''.join(diff))
    for name in ('distance_controller.cpp','encoder.cpp','motor_controller.cpp','wifi_hotspot.cpp'):
        assert (ROOT/'src'/name).read_bytes() == (PROJECT/'src'/name).read_bytes()
    print('Temporary interface added; production control and update loop are byte-identical.')

if __name__ == '__main__':
    if '--extend' in sys.argv: extend()
    else: prepare()
