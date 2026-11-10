"""Bounded serial trials for the symmetric-band retuning study; no Wi-Fi access."""
import argparse
import json
from pathlib import Path
from run_unloaded import Rig

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('output',type=Path)
    parser.add_argument('--distances',nargs='+',type=float,default=[10,20,50,100])
    parser.add_argument('--repeats',type=int,default=1)
    parser.add_argument('--adapt',action='store_true')
    parser.add_argument('--require-continuous',action='store_true')
    parser.add_argument('--stop-tests',action='store_true')
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    results=[]
    with (args.output/'trials.log').open('x',encoding='utf-8') as log:
        rig=Rig(log,endpoint_band=(-.5,.5))
        try:
            rig.send('STOP');rig.send('LOG ON');rig.send('ADAPT RESET')
            rig.send('ADAPT ON' if args.adapt else 'ADAPT OFF');rig.wait(2)
            cases=([(100,1,'serial'),(100,-1,'heartbeat'),(100,1,'serial_cruise'),
                    (100,-1,'serial_approach'),(100,-1,'heartbeat_cruise')] if args.stop_tests else
                   [(d,direction,None) for d in args.distances for direction in (1,-1) for _ in range(args.repeats)])
            for distance,direction,stop in cases:
                result=rig.move(distance,direction,stop)
                travel=[r for r in rig.rows if r['boot']==result['boot'] and r['move']==result['move'] and
                        r['phase'] in ('TRAVELLING','APPROACHING','RECOVERING') and r['remaining_cm']>.5001]
                result['max_interior_pulse_age_s']=max((r['pulse_age_us']/1e6 for r in travel if r['pulse_age_us']<4294967295),default=0)
                result['continuous_screen_pass']=result['pass'] and result['recoveries']==0 and result['max_interior_pulse_age_s']<1
                results.append(result)
                (args.output/'trials.json').write_text(json.dumps(results,indent=2))
                if not result['pass'] or (args.require_continuous and not result['continuous_screen_pass']):
                    print('Screening failed; no further moves in this series.',flush=True)
                    return
        finally:rig.close()

if __name__=='__main__':main()
