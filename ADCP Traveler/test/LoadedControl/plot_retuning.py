"""Plot measured retuning evidence, preserving all stages and failures."""
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
from matplotlib.ticker import ScalarFormatter,NullLocator

plt.rcParams.update({'figure.dpi':120,'savefig.dpi':150,'font.size':10,'axes.spines.top':False,'axes.spines.right':False})

def load(stage):
    groups=collections.defaultdict(list);bad=0
    for line in (stage/'trials.log').read_text(encoding='utf-8').splitlines():
        if line.startswith('GG,'):
            try:r=json.loads(line[3:]);groups[(r['boot'],r['move'])].append(r)
            except ValueError:bad+=1
    metrics=[]
    for m in json.loads((stage/'trials.json').read_text()):
        rows=groups[(m['boot'],m['move'])]
        travel=[r for r in rows if r['phase'] in ('TRAVELLING','APPROACHING','RECOVERING') and r['remaining_cm']>.5001]
        pi=[r for r in rows if r['phase'] in ('TRAVELLING','APPROACHING') and m['direction']*r['command_cm_s']>0]
        m['stage']=stage.name
        m['max_interior_pulse_age_s']=max((r['pulse_age_us']/1e6 for r in travel if r['pulse_age_us']<4294967295),default=0)
        m['speed_rmse_cm_s']=float(np.sqrt(np.mean([(r['velocity_cm_s']-r['command_cm_s'])**2 for r in pi]))) if pi else None
        m['sequence_gaps']=sum(max(0,b['seq']-a['seq']-1) for a,b in zip(rows,rows[1:]))
        m['learning_end_start_v']=rows[-1]['learned_start_v'];m['learning_end_load_v']=rows[-1]['load_v']
        metrics.append(m)
    return metrics,groups,bad

def save(fig,path):
    fig.tight_layout();fig.savefig(path,bbox_inches='tight');plt.close(fig)

def simulation_plots(folder,plots):
    sweep=folder/'simulation-sweep';summaries=[];groups={}
    for stage in ('baseline','candidate-c'):
        table=collections.defaultdict(list)
        for row in csv.DictReader((sweep/(stage+'-traces.csv')).open()):
            table[(row['scenario'],int(row['direction']),int(row['adapt']),int(row['repeat']))].append(row)
        groups[stage]=table
        for key,rows in table.items():
            terminal=next((r for r in rows if r['phase'] in ('COMPLETE','ERROR','STOPPED')),None)
            summaries.append(dict(stage=stage,scenario=key[0],direction=key[1],adapt=bool(key[2]),repeat=key[3],
                phase=terminal['phase'] if terminal else 'INCOMPLETE',failure=terminal['failure'] if terminal else '',
                duration_s=float(terminal['time_s']) if terminal else None,recoveries=max(int(r['recoveries']) for r in rows),
                encoder_error_cm=float(rows[-1]['encoder_cm'])-100,model_wheel_error_cm=float(rows[-1]['position_cm'])-100))
    (sweep/'scenario-summary.json').write_text(json.dumps(summaries,indent=2))
    labels={'constant_load':'Constant resistance','varying_resistance':'Position-varying resistance','load_step':'Resistance step',
            'endpoint_friction':'Increased endpoint friction','temporary_block':'Temporary blockage','permanent_block':'Permanent blockage','assisting_load':'Assisting load'}
    for scenario,label in labels.items():
        fig,axs=plt.subplots(4,1,figsize=(11,10),sharex=True)
        for stage,color in (('baseline','#777777'),('candidate-c','#e6ab02')):
            rows=groups[stage][(scenario,1,0,1)];t=[float(r['time_s']) for r in rows]
            for ax,key in zip(axs,('position_cm','velocity_cm_s','applied_v','recoveries')):
                ax.plot(t,[float(r[key]) for r in rows],color=color,label=stage)
            axs[1].plot(t,[float(r['command_cm_s']) for r in rows],color=color,ls='--',alpha=.6)
        axs[0].axhspan(99.5,100.5,color='#c7e9c0',alpha=.5)
        for ax,label_y in zip(axs,('Model wheel travel (cm)','Model speed / dashed request (cm/s)','Nominal output (V)','Recovery attempts')):
            ax.set_ylabel(label_y);ax.grid(alpha=.2);ax.legend(fontsize=8)
        axs[-1].set_xlabel('Seconds since command');fig.suptitle('SIMULATION ONLY — uncalibrated plant\n'+label+'; first 100 cm forward attempt, adaptation OFF',y=1.01)
        save(fig,plots/('simulation-'+scenario+'.png'))
    variants=json.loads((sweep/'summary.json').read_text())
    fig,ax=plt.subplots(figsize=(14,5))
    ax.bar(range(len(variants)),[v['total_recoveries'] for v in variants],color=['#2166ac' if '-far-' in v['case'] else '#777777' for v in variants])
    ax.set_xticks(range(len(variants)),[v['case'] for v in variants],rotation=60,ha='right',fontsize=8)
    ax.set(ylabel='Total recovery attempts in 16 cases',title='SIMULATION ONLY — 24 parameter combinations × 16 uncalibrated load/distance cases\nBlue: band-aware braking; grey: previous braking geometry');ax.grid(axis='y',alpha=.2)
    save(fig,plots/'simulation-sweep.png')
    return labels,summaries

def main():
    p=argparse.ArgumentParser();p.add_argument('folder',type=Path);args=p.parse_args();folder=args.folder
    plots=folder/'plots';plots.mkdir(exist_ok=True)
    all_metrics=[];stages={};bad=0
    for stage in sorted(folder.iterdir()):
        if stage.is_dir() and (stage/'trials.json').exists():
            metrics,groups,invalid=load(stage);stages[stage.name]=(metrics,groups);all_metrics+=metrics;bad+=invalid
    palette={'baseline':'#777777','candidate-a':'#2166ac','candidate-b':'#d6604d','candidate-c':'#e6ab02','validation-off':'#1b9e77','validation-on':'#984ea3','validation-min-off':'#4daf4a','validation-min-on':'#a65628'}
    fig,axs=plt.subplots(2,2,figsize=(13,8))
    for name,(metrics,groups) in stages.items():
        for direction,marker in ((1,'o'),(-1,'x')):
            ms=[m for m in metrics if m['direction']==direction and not m['stop_test']]
            if not ms:continue
            for ax,key in zip(axs.flat,('encoder_error_cm','duration_s','max_interior_pulse_age_s','recoveries')):
                ax.scatter([m['distance_cm'] for m in ms],[m[key] for m in ms],label=f'{name}, {"RB" if direction==1 else "LB"}',marker=marker,color=palette.get(name),alpha=.7)
    axs[0,0].axhspan(-.5,.5,color='#c7e9c0',alpha=.5);axs[1,0].axhline(1,color='#b2182b',ls='--',lw=.8)
    for ax,label in zip(axs.flat,('Signed encoder error (cm)','Duration (s)','Max sampled pulse age outside band (s)','Recovery attempts')):
        ds=sorted(set(m['distance_cm'] for m in all_metrics));ax.set(xscale='log',xlabel='Requested distance (cm)',ylabel=label)
        ax.set_xticks(ds);ax.xaxis.set_major_formatter(ScalarFormatter());ax.xaxis.set_minor_locator(NullLocator());ax.tick_params(axis='x',labelsize=8,rotation=45);ax.grid(alpha=.2)
    axs[0,1].legend(fontsize=7,loc='upper left',bbox_to_anchor=(1.02,1));fig.suptitle('MEASURED unloaded shaft — symmetric ±0.5 cm band; sampled USB evidence',y=1.01)
    save(fig,plots/'comparison.png')
    for direction in (1,-1):
        fig,axs=plt.subplots(3,1,figsize=(11,9),sharex=True)
        for name,(metrics,groups) in stages.items():
            ms=[m for m in metrics if m['distance_cm']==50 and m['direction']==direction]
            if not ms:continue
            m=ms[0];rows=groups[(m['boot'],m['move'])];t=[r['elapsed_us']/1e6 for r in rows]
            axs[0].plot(t,[direction*(r['position_cm']-r['start_cm']) for r in rows],label=name,color=palette.get(name))
            axs[1].plot(t,[direction*r['velocity_cm_s'] for r in rows],label=name,color=palette.get(name))
            axs[2].plot(t,[direction*r['applied_v'] for r in rows],label=name,color=palette.get(name))
        axs[0].axhspan(49.5,50.5,color='#c7e9c0',alpha=.5)
        for ax,label in zip(axs,('Encoder travel (cm)','Filtered speed (cm/s)','Nominal applied output (V)')):
            ax.set_ylabel(label);ax.grid(alpha=.2);ax.legend(fontsize=8)
        axs[-1].set_xlabel('Seconds since command');fig.suptitle(f'MEASURED 50 cm {"RB" if direction==1 else "LB"} — first attempt of each stage',y=1.01)
        save(fig,plots/f'50cm-{"RB" if direction==1 else "LB"}.png')
    links=[]
    for name,(metrics,groups) in stages.items():
        for m in metrics:
            rows=groups[(m['boot'],m['move'])];direction=m['direction'];t=np.array([r['elapsed_us']/1e6 for r in rows])
            fig,axs=plt.subplots(4,1,figsize=(11,10),sharex=True)
            travel=np.array([direction*(r['position_cm']-r['start_cm']) for r in rows])
            axs[0].plot(t,travel);axs[0].axhspan(m['distance_cm']-.5,m['distance_cm']+.5,color='#c7e9c0',alpha=.5)
            axs[0].axhline(m['distance_cm'],color='black',ls='--',lw=.8);axs[0].set_ylabel('Encoder travel (cm)')
            if not m['stop_test']:
                inset=axs[0].inset_axes([.68,.1,.29,.48]);inset.plot(t,travel-m['distance_cm']);inset.axhspan(-.5,.5,color='#c7e9c0',alpha=.5)
                inset.set(xlim=(max(0,t[-1]-8),t[-1]),ylim=(-2,1),title='Endpoint error (cm)');inset.tick_params(labelsize=7);inset.title.set_fontsize(8)
            axs[1].plot(t,[direction*r['velocity_cm_s'] for r in rows],label='Encoder estimate');axs[1].plot(t,[direction*r['command_cm_s'] for r in rows],ls='--',label='Request');axs[1].set_ylabel('Speed (cm/s)')
            for key,label in (('applied_v','Applied'),('requested_v','Requested'),('integral_v','Integral'),('load_v','Learned load')):
                axs[2].plot(t,[direction*r[key] if key in ('applied_v','requested_v') else r[key] for r in rows],label=label)
            axs[2].set_ylabel('Nominal V')
            phases=list(dict.fromkeys(r['phase'] for r in rows));axs[3].step(t,[phases.index(r['phase']) for r in rows],where='post')
            axs[3].set_yticks(range(len(phases)),phases);axs[3].set_xlabel('Seconds since command')
            for ax in axs:ax.grid(alpha=.2)
            axs[1].legend(fontsize=8);axs[2].legend(fontsize=8,ncol=4)
            filename=f'{name}-{m["boot"]}-{m["move"]:03d}.png'
            fig.suptitle(f'MEASURED {name}: {m["distance_cm"]} cm {"RB" if direction==1 else "LB"} — {m["phase"]} {m["failure"]}\n{m["stop_test"] or "Positioning trial"}',y=1.01)
            save(fig,plots/filename);links.append((m,filename))
    (folder/'all-metrics.json').write_text(json.dumps(all_metrics,indent=2))
    fields=list(dict.fromkeys(key for m in all_metrics for key in m))
    with (folder/'all-metrics.csv').open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader();writer.writerows(all_metrics)
    page='''<!doctype html><html><head><meta charset="utf-8"><title>Continuous distance retuning</title><style>body{font:16px system-ui;max-width:1150px;margin:40px auto;padding:0 20px;color:#18232d}img{max-width:100%}table{border-collapse:collapse;width:100%}td,th{border:1px solid #ddd;padding:7px;text-align:left}.note{background:#fff4d6;padding:18px}</style></head><body><h1>Continuous unloaded distance retuning</h1><p>10 November 2026 · USB only · original app retained · fixed target · ±0.5 cm encoder band.</p><p class="note">Measured encoder-equivalent shaft travel is not traveller displacement. Pulse gaps and velocity estimates are sampled at about 5 Hz; absence of long gaps does not prove perfectly continuous sub-pulse motion. Voltage is a nominal PWM equivalent. Simulated loads are uncalibrated and do not validate towing, traction, rope sag or physical accuracy. See README.md for final operational status and findings.</p><p><a href="README.md">Findings</a> · <a href="all-metrics.csv">All metrics CSV</a> · <a href="all-metrics.json">All metrics JSON</a></p><h2>Measured comparison</h2><p>Candidate A: old PI/feedforward, 3 cm/s approach, band-aware braking. B: fitted feedforward 2.3 V +17.5 V/(m/s), Ki=2. C: same feedforward with Ki=4. All retain Kp=13, 20 cm/s maximum request, 10 cm/s² acceleration request and 5 cm/s² planning deceleration. Validation uses C unchanged across all distances/directions. The model and experiment do not establish a globally optimal gain set.</p><img src="plots/comparison.png"><img src="plots/50cm-RB.png"><img src="plots/50cm-LB.png"><table><tr><th>Stage</th><th>cm / direction</th><th>Result</th><th>Error cm</th><th>Time s</th><th>Recoveries</th><th>Pulse age s</th><th>Trace</th></tr>'''
    for m,filename in links:
        page+=f'<tr><td>{html.escape(m["stage"])}</td><td>{m["distance_cm"]} / {m["direction"]:+d}</td><td>{m["phase"]} {m["failure"]} {m["stop_test"] or ""}</td><td>{m["encoder_error_cm"]:.4f}</td><td>{m["duration_s"]:.2f}</td><td>{m["recoveries"]}</td><td>{m["max_interior_pulse_age_s"]:.2f}</td><td><a href="plots/{filename}">Graph</a></td></tr>'
    page+='</table><p>Raw logs, settings and source identities are retained in each stage folder. Capture records with invalid/truncated JSON: '+str(bad)+'.</p>'
    labels,summaries=simulation_plots(folder,plots)
    page+='<h2>Uncalibrated simulation experiments</h2><p>The parameter sweep uses forward-sign synthetic travel, four resisting-load equivalents and 10/20/50/100 cm requests (16 cases per variant). Both signs and adaptation states are included in the separate seven-scenario traces. The plant uses acceleration=24×(output−resistance)−3×speed, plus static breakaway resistance and quantized encoder pulses. It is not fitted to this motor and does not reproduce all hardware tracking behaviour; absolute model errors were not used to rank A/B.</p><p><a href="simulation-sweep/all-cases.csv">384 sweep cases</a> · <a href="simulation-sweep/scenario-summary.json">Scenario summary</a> · <a href="simulation-sweep/baseline-traces.csv">Baseline model traces</a> · <a href="simulation-sweep/candidate-c-traces.csv">Candidate C model traces</a></p><img src="plots/simulation-sweep.png">'
    for scenario,label in labels.items():
        page+='<h3>'+label+'</h3>'
        for stage in ('baseline','candidate-c'):
            ms=[m for m in summaries if m['stage']==stage and m['scenario']==scenario]
            outcomes=collections.Counter((m['phase'],m['failure']) for m in ms)
            page+='<p>'+html.escape(stage+': '+str(dict(outcomes))+'; total recoveries='+str(sum(m['recoveries'] for m in ms)))+'</p>'
        page+='<img src="plots/simulation-'+scenario+'.png">'
    page+='</body></html>'
    (folder/'index.html').write_text(page,encoding='utf-8')
    print(json.dumps({name:{'trials':len(ms),'passes':sum(m['pass'] for m in ms),'recoveries':sum(m['recoveries'] for m in ms),'max_interior_pulse_age_s':max(m['max_interior_pulse_age_s'] for m in ms)} for name,(ms,_) in stages.items()},indent=2))

if __name__=='__main__':main()
