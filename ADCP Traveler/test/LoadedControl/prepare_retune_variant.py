"""Prepare isolated parameter/planner experiments without changing production."""
import argparse
import difflib
import hashlib
import json
import re
import shutil
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]

def main():
    p=argparse.ArgumentParser();p.add_argument('name')
    p.add_argument('--approach',type=float,default=3)
    p.add_argument('--friction',type=float,default=1.8)
    p.add_argument('--feedforward',type=float,default=23)
    p.add_argument('--kp',type=float,default=13);p.add_argument('--ki',type=float,default=2)
    args=p.parse_args()
    baseline=ROOT/'.pio/retune-half-cm/baseline';project=baseline.parent/args.name
    out=ROOT/'test/LoadedControl/results/retune-2026-11-10'/args.name
    project.mkdir(parents=True,exist_ok=False);out.mkdir(parents=True,exist_ok=True)
    for directory in ('src','include'):
        shutil.copytree(baseline/directory,project/directory)
    shutil.copy2(baseline/'platformio.ini',project/'platformio.ini')
    (project/'.pio').mkdir();shutil.copytree(ROOT/'.pio/libdeps',project/'.pio/libdeps')
    config=project/'include/config.h';text=config.read_text()
    parameters={'APPROACH_SPEED_CM_S':args.approach,'FRICTION_COMPENSATION_V':args.friction,
                'FEEDFORWARD_GAIN_V_PER_M_S':args.feedforward,'VELOCITY_KP':args.kp,'VELOCITY_KI':args.ki}
    for name,value in parameters.items():
        text,n=re.subn(r'(constexpr float '+name+r' = )[^;]+;',lambda m:m[1]+str(float(value))+'f;',text)
        assert n==1,name
    config.write_text(text)
    control=project/'src/distance_controller.cpp';text=control.read_text()
    old='remaining - Config::Control::POSITION_TOLERANCE_CM * .5f -'
    assert old in text
    text=text.replace(old,'remaining + Config::Control::POSITION_TOLERANCE_CM -')
    control.write_text(text)
    # Host exercise uses copied production sources and the current regression cases.
    host=(ROOT/'test/LoadedControl/test_control.cpp').read_text().replace('../../src/',(project/'src').as_posix()+'/')
    (project/'controller-tests.cpp').write_text(host)
    patch=[]
    for rel in ('include/config.h','src/distance_controller.cpp'):
        patch.extend(difflib.unified_diff((baseline/rel).read_text().splitlines(True),(project/rel).read_text().splitlines(True),fromfile='baseline/'+rel,tofile=args.name+'/'+rel))
    (out/'variant.patch').write_text(''.join(patch))
    (out/'settings.json').write_text(json.dumps({'parameters':parameters,'planner':'far permitted endpoint edge minus unchanged margin and reaction allowance','endpoint_band_cm':[-.5,.5],'source_sha256':{str(f.relative_to(project)):hashlib.sha256(f.read_bytes()).hexdigest() for folder in ('src','include') for f in (project/folder).glob('*') if f.is_file()}},indent=2))
    print(project)

if __name__=='__main__':main()
