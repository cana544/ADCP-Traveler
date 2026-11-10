"""Produce static plots and an HTML evidence report; never communicate with hardware."""
import argparse
import collections
import csv
import html
import json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter

plt.rcParams.update({'figure.dpi':130,'savefig.dpi':160,'font.size':10,'axes.spines.top':False,'axes.spines.right':False})
TERMINAL={'COMPLETE','ERROR','STOPPED'}
LABELS={'constant_load':'Constant resistance','varying_resistance':'Position-dependent resistance','load_step':'Resistance step at 25 cm',
        'endpoint_friction':'Higher friction after 95 cm','temporary_block':'Four-second blockage','permanent_block':'Permanent blockage','assisting_load':'Sustained assisting load'}

def save(fig,path):
    fig.tight_layout();fig.savefig(path,bbox_inches='tight');plt.close(fig)

def tracking_metrics(rows,direction):
    travel=[r for r in rows if r['phase'] in ('TRAVELLING','APPROACHING') and direction*r['command_cm_s']>0]
    errors=[direction*(r['velocity_cm_s']-r['command_cm_s']) for r in travel]
    return (float(np.sqrt(np.mean(np.square(errors)))) if errors else None,
            sum(abs(r['velocity_cm_s'])<.01 for r in travel))

def hardware(folder):
    metrics=[];groups=collections.defaultdict(list);rejected=0
    for path in sorted(folder.glob('*.log')):
        if path.name.endswith('-upload.log'):continue # PowerShell build logs are not UTF-8 telemetry.
        for line in path.read_text(encoding='utf-8').splitlines():
            if line.startswith('GG,'):
                try:
                    r=json.loads(line[3:]);groups[(r['boot'],r['move'])].append(r)
                except (ValueError,KeyError): rejected+=1
    for path in sorted(folder.glob('*.json')):
        if path.stem in ('matrix','pilot','stops','stop-phases','followup'):
            metrics.extend(json.loads(path.read_text()))
    normal=[m for m in metrics if not m['stop_test']]
    plots=folder/'plots';plots.mkdir(exist_ok=True)
    if normal:
        fig,axs=plt.subplots(2,2,figsize=(12,8))
        for adapt,color in ((False,'#2166ac'),(True,'#d6604d')):
            ms=[m for m in normal if m['adapt']==adapt]
            if not ms:continue
            for direction,marker in ((1,'o'),(-1,'x')):
                subset=[m for m in ms if m['direction']==direction]
                for ax,key in zip(axs.flat,('encoder_error_cm','duration_s','recoveries','peak_applied_v')):
                    ax.scatter([m['distance_cm'] for m in subset],[m[key] for m in subset],label=f'Adapt {"ON" if adapt else "OFF"}, dir {direction:+d}',marker=marker,color=color,alpha=.75)
        axs[0,0].axhspan(-1,0,color='#c7e9c0',alpha=.5,label='Encoder acceptance band')
        for ax,label in zip(axs.flat,('Signed encoder endpoint error (cm)','Move duration (s)','Recovery attempts','Peak sampled nominal output (V)')):
            ax.set(xlabel='Requested encoder-equivalent distance (cm)',ylabel=label,xscale='log');ax.grid(alpha=.2)
            ax.set_xticks(sorted(set(m['distance_cm'] for m in normal)))
            ax.xaxis.set_major_formatter(ScalarFormatter());ax.xaxis.set_minor_locator(matplotlib.ticker.NullLocator())
        axs[0,0].legend(fontsize=7)
        fig.suptitle('MEASURED unloaded shaft — sampled USB telemetry',y=1.02)
        save(fig,plots/'hardware-summary.png')
        distances=sorted(set(m['distance_cm'] for m in normal))
        fig,axs=plt.subplots(len(distances),1,figsize=(11,max(3,2.6*len(distances))),squeeze=False)
        for ax,d in zip(axs[:,0],distances):
            for m in normal:
                if m['distance_cm']!=d:continue
                rows=groups[(m['boot'],m['move'])];direction=m['direction']
                t=np.array([r['elapsed_us']/1e6 for r in rows])
                ax.plot(t,[direction*r['velocity_cm_s'] for r in rows],color='#d6604d' if m['adapt'] else '#2166ac',alpha=.45,lw=.9)
                ax.plot(t,[direction*r['command_cm_s'] for r in rows],color='black',alpha=.18,lw=.8,ls='--')
            ax.set(title=f'{d:g} cm — blue adaptation OFF; orange ON; dashed request',xlabel='Time since move start (s)',ylabel='Speed (cm/s)');ax.grid(alpha=.2)
        save(fig,plots/'hardware-velocity-overlays.png')
        derived=[]
        for m in normal:
            rows=groups[(m['boot'],m['move'])]
            rmse,zeros=tracking_metrics(rows,m['direction'])
            derived.append(dict(boot=m['boot'],move=m['move'],distance_cm=m['distance_cm'],direction=m['direction'],adapt=m['adapt'],
                velocity_rmse_cm_s=rmse,
                learned_start_v=rows[-1]['learned_start_v'],learned_load_v=rows[-1]['load_v'],
                zero_speed_travel_samples=zeros,
                sequence_gaps=sum(max(0,b['seq']-a['seq']-1) for a,b in zip(rows,rows[1:]))))
        (folder/'hardware-derived.json').write_text(json.dumps(derived,indent=2))
        fig,axs=plt.subplots(3,1,figsize=(11,9),sharex=True)
        for adapt,color in ((False,'#2166ac'),(True,'#d6604d')):
            for direction,marker in ((1,'o'),(-1,'x')):
                ds=[d for d in derived if d['adapt']==adapt and d['direction']==direction]
                for ax,key in zip(axs,('velocity_rmse_cm_s','learned_start_v','learned_load_v')):
                    ds_valid=[d for d in ds if d[key] is not None]
                    ax.scatter([d['move'] for d in ds_valid],[d[key] for d in ds_valid],color=color,marker=marker,label=f'Adapt {adapt}, dir {direction:+d}')
        for ax,label in zip(axs,('PI-travel speed RMSE (cm/s)','Direction-specific learned startup (V)','Active learned load bias (V)')):
            ax.set_ylabel(label);ax.grid(alpha=.2)
        axs[0].legend(fontsize=8);axs[-1].set_xlabel('Move identity within capture boot')
        fig.suptitle('MEASURED — tracking and learning (RMSE excludes startup/recovery/settling)',y=1.01)
        save(fig,plots/'hardware-tracking-learning.png')
        historical_path=Path(__file__).resolve().parents[1]/'DistanceControlTest/results/summary.csv'
        if historical_path.exists():
            historical=[r for r in csv.DictReader(historical_path.open()) if r['stage']=='validation']
            common=sorted(set(m['distance_cm'] for m in normal)&set(float(r['distance']) for r in historical))
            fig,axs=plt.subplots(1,2,figsize=(12,4))
            for d in common:
                old=[r for r in historical if float(r['distance'])==d]
                new=[m for m in normal if m['distance_cm']==d and not m['adapt']]
                for ax,ok,nk in zip(axs,('final_error_cm','duration_s'),('encoder_error_cm','duration_s')):
                    ax.scatter([d*.98]*len(old),[float(r[ok]) for r in old],color='#888888',marker='x',alpha=.6)
                    ax.scatter([d*1.02]*len(new),[r[nk] for r in new],color='#2166ac',alpha=.6)
            axs[0].axhspan(-1,0,color='#c7e9c0',alpha=.4)
            for ax,label in zip(axs,('Signed encoder error (cm)','Duration (s)')):
                ax.set(xscale='log',xlabel='Requested distance (cm)',ylabel=label);ax.grid(alpha=.2)
                ax.set_xticks(common);ax.xaxis.set_major_formatter(ScalarFormatter());ax.xaxis.set_minor_locator(matplotlib.ticker.NullLocator())
            fig.suptitle('Historical context only — grey: Oct standalone; blue: current production, adaptation OFF\nDifferent controller and endpoint policy; separate sessions, not controlled A/B',y=1.1)
            save(fig,plots/'historical-context.png')
    trace_links=[]
    for m in metrics:
        rows=groups[(m['boot'],m['move'])]
        if not rows:continue
        t=np.array([r['elapsed_us']/1e6 for r in rows]);direction=m['direction']
        fig,axs=plt.subplots(4,1,figsize=(11,10),sharex=True)
        progress=[direction*(r['position_cm']-r['start_cm']) for r in rows]
        axs[0].plot(t,progress,label='Encoder travel');axs[0].axhline(m['distance_cm'],color='black',ls='--',label='Target')
        axs[0].axhspan(m['distance_cm']-1,m['distance_cm'],color='#c7e9c0',alpha=.5);axs[0].set_ylabel('Travel (cm)')
        if not m['stop_test']:
            inset=axs[0].inset_axes([.68,.1,.29,.48])
            inset.plot(t,np.asarray(progress)-m['distance_cm'],lw=1)
            inset.axhspan(-1,0,color='#c7e9c0',alpha=.5);inset.axhline(0,color='black',ls='--',lw=.7)
            inset.set(xlim=(max(0,t[-1]-9),t[-1]),ylim=(-3,.5),title='Endpoint error (cm)')
            inset.tick_params(labelsize=7);inset.title.set_fontsize(8);inset.grid(alpha=.2)
        axs[1].plot(t,[direction*r['velocity_cm_s'] for r in rows],label='Encoder estimate');axs[1].plot(t,[direction*r['command_cm_s'] for r in rows],ls='--',label='Requested');axs[1].set_ylabel('Speed (cm/s)')
        for key,label in (('applied_v','Applied nominal V'),('requested_v','Requested V'),('load_v','Learned load V')):
            axs[2].plot(t,[direction*r[key] if key in ('applied_v','requested_v') else r[key] for r in rows],label=label)
        axs[2].set_ylabel('Nominal V')
        phases=list(dict.fromkeys(r['phase'] for r in rows));axs[3].step(t,[phases.index(r['phase']) for r in rows],where='post',label='Phase')
        axs[3].set_yticks(range(len(phases)),phases);axs[3].set_xlabel('Time since move start (s)')
        for ax in axs:ax.grid(alpha=.2);ax.legend(loc='upper left',fontsize=8)
        name=f"hardware-{m['boot']}-{m['move']:03d}.png"
        fig.suptitle(f"MEASURED: {m['distance_cm']} cm, dir {direction:+d}, adaptation {m['adapt']} — {m['phase']} {m['failure']}\n{m['stop_test'] or 'Positioning trial'}",y=1.01)
        save(fig,plots/name);trace_links.append((m,name))
    if metrics:
        with (folder/'hardware-summary.csv').open('w',newline='') as f:
            fields=list(dict.fromkeys(k for m in metrics for k in m))
            writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader();writer.writerows(metrics)
    return metrics,trace_links,rejected

def simulation(folder):
    groups=collections.defaultdict(list)
    for row in csv.DictReader((folder/'simulation.csv').open()):
        groups[(row['scenario'],int(row['direction']),int(row['adapt']),int(row['repeat']))].append(row)
    summaries=[]
    for key,rows in groups.items():
        terminal=next((r for r in rows if r['phase'] in TERMINAL),None)
        last=rows[-1]
        summaries.append(dict(scenario=key[0],direction=key[1],adapt=bool(key[2]),repeat=key[3],phase=terminal['phase'] if terminal else 'INCOMPLETE',
                              failure=terminal['failure'] if terminal else '',duration_s=float(terminal['time_s']) if terminal else None,
                              encoder_error_cm=float(last['encoder_cm'])-100,model_position_error_cm=float(last['position_cm'])-100,
                              recoveries=max(int(r['recoveries']) for r in rows)))
    (folder/'simulation-summary.json').write_text(json.dumps(summaries,indent=2))
    with (folder/'simulation-summary.csv').open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(summaries[0]));writer.writeheader();writer.writerows(summaries)
    for scenario in LABELS:
        fig,axs=plt.subplots(4,1,figsize=(11,11),sharex=True)
        for adapt,color in ((0,'#2166ac'),(1,'#d6604d')):
            rows=groups[(scenario,1,adapt,1)]
            x=np.array([float(r['time_s']) for r in rows])
            for ax,key in zip(axs[:3],('position_cm','velocity_cm_s','applied_v')):
                ax.plot(x,[float(r[key]) for r in rows],color=color,label=f'Adaptation {"ON" if adapt else "OFF"}')
            axs[1].plot(x,[float(r['command_cm_s']) for r in rows],color=color,ls='--',alpha=.6)
            axs[3].step(x,[int(r['recoveries']) for r in rows],color=color,where='post',label=f'Recoveries, adapt {adapt}')
        axs[0].axhline(100,color='black',ls='--');axs[0].axhspan(99,100,color='#c7e9c0',alpha=.5)
        for ax,label in zip(axs,('Model wheel travel (cm)','Model speed; dashed request (cm/s)','Nominal motor output (V)','Recovery count')):
            ax.set_ylabel(label);ax.grid(alpha=.2);ax.legend(fontsize=8,loc='upper left')
        axs[-1].set_xlabel('Time since command (s)')
        fig.suptitle('SIMULATION ONLY — uncalibrated plant\n'+LABELS[scenario]+'; 100 cm; first forward attempt',y=1.01)
        save(fig,folder/'plots'/f'simulation-{scenario}.png')
    return summaries

def main():
    parser=argparse.ArgumentParser();parser.add_argument('folder',type=Path);args=parser.parse_args();folder=args.folder
    metrics,traces,rejected=hardware(folder);sims=simulation(folder)
    normal=[m for m in metrics if not m['stop_test']];stops=[m for m in metrics if m['stop_test']]
    complete=sum(m['pass'] for m in normal)
    restoration_path=folder/'restoration-verification.json'
    status='Testing in progress; use the final README for completion status.'
    if restoration_path.exists():
        restoration=json.loads(restoration_path.read_text())
        status=f"Testing complete. Production firmware restored; {restoration['idle_samples']} fresh idle samples showed zero PWM and constant position. Temporary TEST PING was rejected."
    text=f'''<!doctype html><html><head><meta charset="utf-8"><title>Traveller control test evidence</title>
<style>body{{font:16px system-ui;max-width:1150px;margin:40px auto;padding:0 20px;color:#18232d}}img{{max-width:100%}}table{{border-collapse:collapse;width:100%}}td,th{{border:1px solid #ddd;padding:8px;text-align:left}}.note{{background:#fff4d6;padding:18px}}a{{color:#1765a6}}</style></head><body>
<h1>Traveller controller: unloaded tests and synthetic dynamics</h1>
<p>Hardware measured on 10 November 2026. USB only; unloaded shaft. Controller code unchanged.</p>
<p><strong>{status}</strong> <a href="README.md">Detailed findings and limitations</a></p>
<p class="note">Hardware results measure encoder-equivalent wheel travel, not traveller displacement. Simulation uses an uncalibrated synthetic plant and does not validate rope traction, boat drag, slip, changing rope geometry, or loaded accuracy. The IMU is not providing valid reports. Hardware app/Section Control actions were not exercised; their integration was checked in software.</p>
<h2>What the new controller is testing</h2><p>The current controller still uses velocity PI plus feedforward. Its main change is how a move progresses: an initial effort ramp establishes encoder motion; a position-based braking envelope continuously replans requested speed; loss of progress can trigger a bounded pause and effort ramp while retaining the original absolute target. A fixed velocity-profile timer no longer decides that an unfinished move has completed. Direction-specific startup/load adaptation is an additional feature, not a replacement for encoder feedback or a traction measurement.</p>
<p>Configured limits: requested speed 20 cm/s, request acceleration 10 cm/s², planning deceleration 5 cm/s²; approach request at most 2 cm/s within 8 cm. No progress for 2 seconds can trigger recovery, with a 0.7-second pause and 0.5 nominal V/s ramp up to 6 V, at most twice. A detected target crossing faults without commanded reversal. These request and effort limits do not guarantee physical speed, acceleration, stopping distance, or motor current.</p>
<h2>Measured hardware</h2><p>{complete}/{len(normal)} position trials passed screening; {sum(m['pass'] for m in stops)}/{len(stops)} stop trials passed screening. Malformed/truncated diagnostic lines: {rejected}.</p>
<p>{sum(m['recoveries']>0 for m in normal)} positioning moves used automatic recovery under the original command. Successful completion therefore does not imply seamless motion. Learning was active in longer adaptation-on runs, but this small sequential unloaded comparison does not demonstrate an improvement from adaptation.</p>
<p>Position acceptance: COMPLETE, signed final encoder error between -1 and 0 cm, no movement or nonzero PWM observed in the 2.5-second post-terminal window. Stop trials permit passive coasting, require zero sampled PWM after STOPPED, and require the final one-second window to be stationary. Telemetry is about 5 Hz; these are sampled observations. Encoder resolution is about 0.628 cm/pulse. Voltage values are PWM-equivalent nominal voltages.</p>
<p><a href="hardware-summary.csv">Hardware summary CSV</a> · <a href="matrix.log">Raw matrix log</a> · <a href="matrix.json">Matrix metrics</a></p>'''
    if normal:
        text+='<img src="plots/hardware-summary.png"><h3>Velocity overlays</h3><img src="plots/hardware-velocity-overlays.png"><h3>Tracking and learning</h3><img src="plots/hardware-tracking-learning.png"><p>Speed and nominal output traces are normalized along the commanded direction. RMSE compares requested and filtered encoder speed only during positive-speed TRAVELLING/APPROACHING samples. It is descriptive, not a loaded-accuracy acceptance test. Startup and load learning are direction-specific and retained in RAM across attempts within the capture boot.</p>'
        if (folder/'plots/historical-context.png').exists():
            text+='<h3>Earlier unloaded tuning: context</h3><p>October data came from the frozen standalone sketch, which accepted errors on either side within one pulse. The current production controller deliberately accepts a one-sided 0–1 cm band. These different targets and separate sessions prevent a controlled A/B conclusion.</p><img src="plots/historical-context.png">'
    text+='<h3>Every hardware attempt</h3><table><tr><th>Boot / move</th><th>cm / direction</th><th>Adapt</th><th>Result / test</th><th>Encoder error cm</th><th>Retries</th><th>Trace</th></tr>'
    for m,name in traces:
        text+=f"<tr><td>{m['boot']} / {m['move']}</td><td>{m['distance_cm']} / {m['direction']:+d}</td><td>{m['adapt']}</td><td>{m['phase']} {html.escape(m['failure'])} {m['stop_test'] or ''}</td><td>{m['encoder_error_cm']:.4f}</td><td>{m['recoveries']}</td><td><a href='plots/{name}'>4-panel graph</a></td></tr>"
    text+='</table><h2>Synthetic changing-load cases</h2><p>Real production controller and encoder code, actuator-responsive plant at 1 ms, control at 50 ms. Acceleration = 24 × (nominal output − resisting-load equivalent) − 3 × speed. Static breakaway adds 0.35 nominal V. Encoder pulses arise from integrated model motion. These coefficients are arbitrary and have not been fitted to hardware. No rope, slip, current, torque, or body-motion model is included.</p>'
    completed_sims=[s for s in sims if s['phase']=='COMPLETE']
    model_misses=[s for s in completed_sims if not -1<=s['model_position_error_cm']<=0]
    text+=f'<p>Across {len(sims)} synthetic attempts, {len(completed_sims)} reported COMPLETE. Of those, {len(model_misses)} nevertheless missed the one-sided 1 cm band in continuous model wheel travel. Worst completed model-wheel endpoint error: {max(abs(s["model_position_error_cm"]) for s in completed_sims):.3f} cm. Encoder acceptance therefore does not prove physical acceptance, even in this simplified no-slip model.</p>'
    text+='<p>Both directions and adaptation OFF/ON are exercised, up to three sequential attempts. A failed case stops its repeats. Model wheel travel is separate from quantized encoder travel; relative moves can inherit up to a pulse of initial phase uncertainty. Model endpoints can therefore violate physical 1 cm accuracy even when encoder acceptance passes.</p><p><a href="simulation.csv">All simulation traces CSV</a> · <a href="simulation-summary.csv">Simulation summary CSV</a></p>'
    for scenario,label in LABELS.items():
        subset=[m for m in sims if m['scenario']==scenario]
        counts=collections.Counter((m['phase'],m['failure']) for m in subset)
        text+=f'<h3>{label}</h3><p>'+html.escape(str(dict(counts)))+f'</p><img src="plots/simulation-{scenario}.png">'
    text+='</body></html>'; (folder/'index.html').write_text(text,encoding='utf-8')
    print(json.dumps(dict(hardware_attempts=len(metrics),position_passes=complete,position_attempts=len(normal),stop_attempts=len(stops),simulation_attempts=len(sims),report=str(folder/'index.html')),indent=2))

if __name__=='__main__':main()
