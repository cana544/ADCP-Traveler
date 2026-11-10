"""Serial-only unloaded trials against the disposable production-controller build."""
import argparse
import json
import time
from pathlib import Path
import serial

RESULTS = Path(__file__).resolve().parent/'results/unloaded-2026-11-09'
TERMINAL = {'COMPLETE','ERROR','STOPPED'}

class Rig:
    def __init__(self, log, endpoint_band=(-1.0,0.0)):
        self.log = log
        self.endpoint_band = endpoint_band
        self.port = serial.Serial('COM3',115200,timeout=.05,write_timeout=.5)
        self.last = None
        self.rows = []
        self.ping = 0
        self.last_received = time.monotonic()
        time.sleep(2)
    def send(self, command):
        self.log.write('# SEND '+command+'\n'); self.log.flush()
        self.port.write((command+'\n').encode())
    def tick(self, heartbeat=True):
        now = time.monotonic()
        if heartbeat and now-self.ping >= .4:
            self.send('TEST PING'); self.ping=now
        raw = self.port.readline().decode(errors='replace')
        if raw:
            self.log.write(raw); self.log.flush()
            if raw.startswith('GG,'):
                row=json.loads(raw[3:])
                if self.last and row['boot'] != self.last['boot']: raise RuntimeError('Device rebooted during capture')
                self.last=row;self.rows.append(row); self.last_received=now
        if now-self.last_received>3: raise RuntimeError('Telemetry missing for three seconds')
    def wait(self, seconds, heartbeat=True):
        end=time.monotonic()+seconds
        while time.monotonic()<end: self.tick(heartbeat)
    def move(self, distance, direction, stop=None):
        previous=self.last['move']; first=len(self.rows)
        self.send(f'TEST MOVE {distance} {direction}')
        start=time.monotonic(); terminal=None; stop_sent=False; heartbeat_cut=False
        while time.monotonic()-start < min(600,45+distance/2):
            elapsed=time.monotonic()-start
            self.tick(heartbeat=not (stop=='heartbeat' and elapsed>1) and not heartbeat_cut)
            row=self.last
            if row['move']<=previous:
                if elapsed>2: raise RuntimeError('MOVE not accepted')
                continue
            if stop=='serial' and elapsed>1 and not stop_sent:
                self.send('STOP'); stop_sent=True
            if stop=='serial_cruise' and row['phase']=='TRAVELLING' and abs(row['velocity_cm_s'])>5 and not stop_sent:
                self.send('STOP');stop_sent=True
            if stop=='serial_approach' and row['phase']=='APPROACHING' and not stop_sent:
                self.send('STOP');stop_sent=True
            if stop=='heartbeat_cruise' and row['phase']=='TRAVELLING' and abs(row['velocity_cm_s'])>5:
                heartbeat_cut=True
            if row['phase'] in TERMINAL:
                terminal=row; break
        if terminal is None: raise RuntimeError('Host move deadline exceeded')
        self.wait(2.5)
        rows=[r for r in self.rows[first:] if r['move']>previous]
        final=self.last
        error=direction*(final['position_cm']-final['target_cm'])
        post=[r for r in rows if r['us']>=terminal['us']]
        post_movement=max(abs(r['position_cm']-terminal['position_cm']) for r in post)
        result=dict(distance_cm=distance,direction=direction,adapt=terminal['adapt'],stop_test=stop,
                    move=terminal['move'],boot=terminal['boot'],phase=terminal['phase'],failure=terminal['failure'],
                    duration_s=terminal['elapsed_us']/1e6,encoder_error_cm=error,
                    terminal_error_cm=direction*(terminal['position_cm']-terminal['target_cm']),
                    post_terminal_movement_cm=post_movement,post_terminal_max_pwm=max(abs(r['pwm']) for r in post),
                    peak_applied_v=max(abs(r['applied_v']) for r in rows),
                    peak_velocity_cm_s=max(abs(r['velocity_cm_s']) for r in rows),
                    recoveries=max(r['recoveries'] for r in rows),drops=final['drops'],samples=len(rows),
                    learning_samples=sum(r['learning'] for r in rows))
        tail=[r for r in post if r['us']>=post[-1]['us']-1000000]
        final_motion=max(r['position_cm'] for r in tail)-min(r['position_cm'] for r in tail)
        result['final_window_movement_cm']=final_motion
        result['endpoint_band_cm']=list(self.endpoint_band)
        # Coast after STOP is physical motion, not a commanded restart. Require
        # immediate sampled zero output and a stationary final observation window.
        result['pass']=((terminal['phase']=='STOPPED' and final_motion<.001 if stop else
                         terminal['phase']=='COMPLETE' and self.endpoint_band[0]-.0001<=error<=self.endpoint_band[1]+.0001 and post_movement<.001)
                         and result['post_terminal_max_pwm']==0)
        print(json.dumps(result),flush=True)
        return result
    def close(self):
        try:
            self.send('STOP'); self.send('LOG OFF');self.port.flush()
            # Drain the last full diagnostic line and STOP acknowledgment.
            end=time.monotonic()+.5
            while time.monotonic()<end:
                self.log.write(self.port.readline().decode(errors='replace'))
        finally: self.port.close()

def main():
    global RESULTS
    parser=argparse.ArgumentParser();parser.add_argument('stage',choices=['pilot','matrix','stops','stop-phases'])
    parser.add_argument('--output-dir',type=Path,default=RESULTS)
    args=parser.parse_args(); RESULTS=args.output_dir; RESULTS.mkdir(parents=True,exist_ok=True)
    results=[]
    with (RESULTS/(args.stage+'.log')).open('x',encoding='utf-8') as log:
        rig=Rig(log)
        try:
            rig.send('STOP');rig.send('LOG ON');rig.send('ADAPT OFF');rig.send('ADAPT RESET');rig.wait(2)
            if args.stage=='pilot': cases=[(10,1,False,None),(10,-1,False,None)]
            elif args.stage=='stops': cases=[(100,1,False,'serial'),(100,-1,False,'heartbeat')]
            elif args.stage=='stop-phases': cases=[(100,1,False,'serial_cruise'),(100,-1,False,'serial_approach'),(100,-1,False,'heartbeat_cruise')]
            else:
                cases=[(d,direction,False,None) for d in (10,20,30,50,100,250,500) for direction in (1,-1) for _ in range(3)]
                cases += [(d,direction,True,None) for d in (10,50,500) for direction in (1,-1) for _ in range(3)]
            adapt=False
            for distance,direction,enabled,stop in cases:
                if enabled!=adapt:
                    rig.send('ADAPT ON' if enabled else 'ADAPT OFF');rig.send('ADAPT RESET');rig.wait(.5);adapt=enabled
                result=rig.move(distance,direction,stop); results.append(result)
                (RESULTS/(args.stage+'.json')).write_text(json.dumps(results,indent=2))
                if not result['pass']:
                    print('HALTED: failed screening; inspect before continuing.',flush=True); break
        finally: rig.close()

if __name__=='__main__': main()
