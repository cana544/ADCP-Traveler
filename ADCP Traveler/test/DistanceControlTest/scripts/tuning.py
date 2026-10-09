"""Bounded serial trials and measured reports for the unloaded distance rig."""
import argparse
import csv
import html
import hashlib
import json
import math
import time
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import serial

ROOT = Path(__file__).resolve().parents[1] / "results"
PULSE_CM = 2 * math.pi * 5 / 50
DEFAULTS = dict(vmax=20., amax=10., positionKp=3., velocityKp=13.,
                velocityKi=2., feedforward=23., friction=1.8)
FIELDS = "time_s,distance_cm,velocity_cm_s,effort_v,pwm,ref_velocity_cm_s,velocity_cmd_cm_s,ref_position_cm,position_error_cm,state,pulse_age_us,pulse_period_us,pulses".split(",")


def profile_duration(distance, vmax, amax):
    if not all(math.isfinite(x) and x > 0 for x in (distance, vmax, amax)):
        raise ValueError("Invalid profile inputs")
    return 2*math.sqrt(distance/amax) if distance <= vmax*vmax/amax else distance/vmax+vmax/amax


def parse_sample(line):
    values = line.strip().split(",")
    if len(values) != len(FIELDS):
        raise ValueError("Wrong telemetry column count")
    result = {}
    for key, value in zip(FIELDS, values):
        if key == "state":
            if value not in ("MOVING", "SETTLING", "COMPLETE", "FAILED"):
                raise ValueError("Invalid state")
            result[key] = value
        else:
            result[key] = float(value)
            if not math.isfinite(result[key]):
                raise ValueError("Nonfinite telemetry")
    return result


def analyse(rows, distance, direction, outcome, settings):
    result = dict(outcome=outcome, samples=len(rows), screen_pass=False,
                  screen_reasons=["no telemetry samples"] if not rows else [])
    if not rows:
        return result
    a = {k: np.array([r[k] for r in rows], dtype=float) for k in FIELDS if k != "state"}
    duration = profile_duration(distance, settings["vmax"], settings["amax"])
    t = a["time_s"]
    active = t < duration - 0.001
    peak = min(settings["vmax"], math.sqrt(distance*settings["amax"]))
    error = a["velocity_cm_s"] - a["ref_velocity_cm_s"]
    rmse = float(np.sqrt(np.mean(error[active]**2))) if active.any() else None
    # Exclude the unavoidable sparse pulses near the start and the endpoint.
    interior = active & (np.abs(a["ref_velocity_cm_s"]) >= max(3., .2*peak))
    established = np.maximum.accumulate(np.abs(a["velocity_cm_s"]) >= .3)
    false_stop = interior & established & (np.abs(a["velocity_cm_s"]) < .3)
    # Raw pulse gap > three expected pulse periods indicates physical stall,
    # independent of the plotted filter. A minimum gap avoids quantisation noise.
    expected_period = PULSE_CM / np.maximum(np.abs(a["ref_velocity_cm_s"]), .1) * 1e6
    pulse_stall = interior & (a["pulse_age_us"] > np.maximum(250000, 3*expected_period))
    def segments(mask):
        return int(np.sum(mask & ~np.r_[False, mask[:-1]]))
    stopped_pwm = np.abs(a["pwm"]) == 0
    restart = (~stopped_pwm[1:]) & stopped_pwm[:-1] & interior[1:] & (t[1:] > .25)
    final_error = direction*distance-a["distance_cm"][-1]
    first_motion = np.flatnonzero(np.abs(a["distance_cm"]) >= .5*PULSE_CM)
    startup_delay = float(t[first_motion[0]]) if len(first_motion) else float(t[-1])
    overshoot = max(0., float(np.max(direction*a["distance_cm"])-distance))
    settled = t >= duration
    post_mask = settled & (t >= t[-1]-.5)
    post_motion = float(np.ptp(a["distance_cm"][post_mask])) if post_mask.any() else None
    completed = np.array([r["state"] == "COMPLETE" for r in rows])
    completion_capture = float(t[-1]-t[np.flatnonzero(completed)[0]]) if completed.any() else 0.0
    completion_motion = float(np.ptp(a["distance_cm"][completed])) if completed.any() else None
    result.update(final_error_cm=float(final_error), overshoot_cm=overshoot,
                  velocity_rmse_cm_s=rmse, normalised_rmse=rmse/peak if rmse is not None else None,
                  peak_velocity_error_cm_s=float(np.max(np.abs(error[active]))) if active.any() else 0,
                  expected_duration_s=duration, duration_s=float(t[-1]),
                  intermediate_stops=segments(false_stop | pulse_stall),
                  restart_kicks=int(np.sum(restart)), post_stop_motion_cm=post_motion,
                  post_stop_pwm_max=int(np.max(np.abs(a["pwm"][settled]))) if settled.any() else None,
                  peak_pwm=int(np.max(np.abs(a["pwm"]))),
                  startup_delay_s=startup_delay,
                  post_completion_capture_s=completion_capture,
                  post_completion_motion_cm=completion_motion,
                  max_position_tracking_error_cm=float(np.max(np.abs(a["position_error_cm"][active]))) if active.any() else 0,
                  profile_type="TRIANGULAR" if distance <= settings["vmax"]**2/settings["amax"] else "TRAPEZOIDAL")
    result["screen_pass"] = bool(outcome == "COMPLETE" and abs(final_error) <= PULSE_CM+.0001
        and result["intermediate_stops"] == 0 and result["restart_kicks"] == 0
        and rmse is not None and rmse/peak <= .15 and startup_delay <= max(1., 2*math.sqrt(4*PULSE_CM/settings["amax"]))
        and post_motion <= .0001 and result["post_stop_pwm_max"] == 0)
    result["screen_pass"] = bool(result["screen_pass"] and completion_capture >= .999 and completion_motion <= .0001)
    reasons = result["screen_reasons"]
    if outcome != "COMPLETE": reasons.append("terminal outcome: " + outcome)
    if abs(final_error) > PULSE_CM+.0001: reasons.append("final error exceeds one pulse")
    if rmse is None: reasons.append("no active-profile velocity data")
    elif rmse/peak > .15: reasons.append("velocity RMSE exceeds 15% screening target")
    if result["intermediate_stops"]: reasons.append("intermediate stop detected")
    if result["restart_kicks"]: reasons.append("restart kick detected")
    if startup_delay > max(1., 2*math.sqrt(4*PULSE_CM/settings["amax"])): reasons.append("excessive startup delay")
    if completion_capture < .999: reasons.append("less than one second recorded after COMPLETE")
    if completion_motion is not None and completion_motion > .0001: reasons.append("encoder motion after COMPLETE")
    if result["post_stop_pwm_max"] != 0: reasons.append("zero PWM after profile end not confirmed")
    return result


def plot_trial(folder, rows, metadata):
    if not rows:
        return
    t = [r["time_s"] for r in rows]
    fig, axes = plt.subplots(4, 1, figsize=(11, 11), sharex=True)
    def draw(ax, key, label, **kw):
        ax.plot(t, [r[key] for r in rows], label=label, **kw)
    draw(axes[0], "distance_cm", "Measured")
    draw(axes[0], "ref_position_cm", "Reference", ls="--")
    axes[0].set_ylabel("Position (cm)")
    draw(axes[1], "velocity_cm_s", "Measured")
    draw(axes[1], "ref_velocity_cm_s", "Reference", ls="--")
    draw(axes[1], "velocity_cmd_cm_s", "Command", alpha=.65)
    axes[1].set_ylabel("Velocity (cm/s)")
    draw(axes[2], "position_error_cm", "Reference position error")
    axes[2].axhline(PULSE_CM, color="gray", ls=":")
    axes[2].axhline(-PULSE_CM, color="gray", ls=":")
    axes[2].set_ylabel("Error (cm)")
    draw(axes[3], "effort_v", "Effort (V)")
    pwm_ax = axes[3].twinx()
    draw(pwm_ax, "pwm", "PWM", color="tab:orange", alpha=.65)
    axes[3].set_ylabel("Voltage (V)"); pwm_ax.set_ylabel("Signed PWM")
    pwm_ax.set_ylim(-260, 260)
    for ax in axes:
        ax.grid(alpha=.25); ax.legend(loc="upper right")
    pwm_ax.legend(loc="upper left")
    axes[3].set_xlabel("Time from MOVE (s)")
    s = metadata["settings"]; m = metadata["metrics"]
    fig.suptitle(f"{metadata['id']} | {metadata['distance']} cm | {'CW' if metadata['direction'] == 1 else 'CCW'} | {m.get('profile_type','')} | {m['outcome']}\n"
                 f"vmax={s['vmax']:g}, amax={s['amax']:g}, posKp={s['positionKp']:g}, velKp={s['velocityKp']:g}, Ki={s['velocityKi']:g}, FF={s['feedforward']:g}, friction={s['friction']:g}")
    fig.tight_layout(rect=(0, 0, 1, .94)); fig.savefig(folder/"trace.png", dpi=140); plt.close(fig)
    fig, ax = plt.subplots(figsize=(10, 3))
    ax.plot(t, [r["pulse_period_us"]/1000 for r in rows], label="Latest pulse period")
    ax.plot(t, [r["pulse_age_us"]/1000 for r in rows], label="Pulse age", alpha=.7)
    ax.set(xlabel="Time (s)", ylabel="Pulse timing (ms)", title=metadata["id"])
    ax.legend(); ax.grid(alpha=.25); fig.tight_layout(); fig.savefig(folder/"pulses.png", dpi=120); plt.close(fig)


def run_trial(port, distance, direction, settings, output=ROOT, stage="tuning", repeat=1, heartbeat=True):
    if not 10 <= distance <= 500 or direction not in (-1, 1):
        raise ValueError("Trial outside agreed distance/direction bounds")
    output = Path(output); output.mkdir(parents=True, exist_ok=True)
    index = len(list(output.glob("trial_*/metadata.json")))+1
    trial_id = f"trial_{index:03d}_{stage}_{distance:g}cm_{'CW' if direction == 1 else 'CCW'}_r{repeat}"
    folder = output/trial_id; folder.mkdir()
    metadata = dict(id=trial_id, distance=distance, direction=direction, stage=stage,
                    repeat=repeat, settings=dict(settings), heartbeat_enabled=heartbeat,
                    timestamp=time.strftime("%Y-%m-%dT%H:%M:%S"))
    sketch_root = Path(__file__).resolve().parents[1]
    metadata["firmware_source_sha256"] = hashlib.sha256(
        (sketch_root/"DistanceControlTest.ino").read_bytes() +
        (sketch_root/"tuning_control.h").read_bytes()).hexdigest()
    rows = []; outcome = "HOST_ERROR"; connection = None
    raw = (folder/"serial.log").open("w", encoding="utf-8", buffering=1)
    def send(command):
        raw.write("> " + command + "\n"); connection.write((command+"\n").encode())
    def read():
        line = connection.readline().decode("utf-8", errors="replace").strip()
        if line: raw.write(line+"\n")
        return line
    try:
        connection = serial.Serial(port, 115200, timeout=.1, write_timeout=1)
        time.sleep(1.5)
        send("HELLO")
        deadline = time.monotonic()+5
        while time.monotonic() < deadline:
            if read().startswith("DC_EVENT,READY"): break
        else: raise RuntimeError("No READY handshake")
        send("STOP")
        time.sleep(.6)  # Let any previous coasting finish before a new trial.
        send("CONFIG " + " ".join(str(settings[k]) for k in DEFAULTS))
        deadline = time.monotonic()+3
        while time.monotonic() < deadline:
            line = read()
            if line == "DC_ACK,CONFIG": break
            if line.startswith("DC_ERROR"): raise RuntimeError(line)
        else: raise RuntimeError("No configuration acknowledgement")
        send(f"MOVE {distance:g} {direction}")
        deadline = time.monotonic()+profile_duration(distance, settings["vmax"], settings["amax"])+8
        next_ping = 0
        terminal_deadline = None
        while time.monotonic() < deadline:
            now = time.monotonic()
            if terminal_deadline is not None and now >= terminal_deadline: break
            if heartbeat and now >= next_ping:
                send("PING"); next_ping = now+.4
            line = read()
            if not line: continue
            if line.startswith("DC_EVENT,END,"):
                outcome = line.split(",")[2]
                terminal_deadline = time.monotonic()+1.1
                continue
            if line.startswith("DC_ERROR"):
                outcome = line; break
            if line == ",".join(FIELDS) or line.startswith("DC_EVENT,START,"):
                continue
            else:
                row = parse_sample(line)
                if rows and row["time_s"] <= rows[-1]["time_s"]:
                    raise ValueError("Nonmonotonic trial time")
                rows.append(row)
                if direction*row["distance_cm"] > distance+max(2*PULSE_CM, .05*distance):
                    outcome = "EXCESS_TRAVEL"; break
        else:
            outcome = "HOST_DEADLINE"
        send("STOP")
    except (Exception, KeyboardInterrupt) as exc:
        metadata["host_error"] = str(exc)
        outcome = "HOST_ERROR"
        if isinstance(exc, KeyboardInterrupt): outcome = "INTERRUPTED"
    finally:
        if connection:
            try: send("STOP")
            except Exception: pass
            connection.close()
        raw.close()
        metadata["metrics"] = analyse(rows, distance, direction, outcome, settings)
        with (folder/"data.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=FIELDS); writer.writeheader(); writer.writerows(rows)
        (folder/"metadata.json").write_text(json.dumps(metadata, indent=2, allow_nan=False))
        plot_trial(folder, rows, metadata)
    return metadata


def report(root=ROOT):
    root = Path(root)
    trials = [json.loads(p.read_text()) for p in sorted(root.glob("trial_*/metadata.json"))]
    if not trials: return
    # Reanalyse preserved raw rows with the current metrics definitions.
    # This keeps earlier trials comparable when a diagnostic is corrected.
    for trial in trials:
        data_file = root/trial["id"]/"data.csv"
        with data_file.open() as stream:
            reader = csv.reader(stream); next(reader, None)
            rows = [parse_sample(",".join(row)) for row in reader]
        trial["metrics"] = analyse(rows, trial["distance"], trial["direction"],
                                   trial["metrics"]["outcome"], trial["settings"])
        trial["analysis_version"] = "post-completion-v3"
        (root/trial["id"]/"metadata.json").write_text(json.dumps(trial, indent=2, allow_nan=False))
    good = [t for t in trials if t["metrics"].get("normalised_rmse") is not None]
    metrics = [("normalised_rmse", "Velocity RMSE / reference peak"),
               ("final_error_cm", "Final signed error (cm)"),
               ("overshoot_cm", "Overshoot (cm)"),
               ("intermediate_stops", "Intermediate stops"),
               ("restart_kicks", "Restart kicks"), ("duration_s", "Trial duration (s)")]
    fig, axes = plt.subplots(3, 2, figsize=(13, 11))
    groups = sorted(set((t["distance"], t["direction"]) for t in good))
    for ax, (key, label) in zip(axes.flat, metrics):
        for distance, direction in groups:
            group = [(i+1, t["metrics"].get(key)) for i, t in enumerate(trials)
                     if t["distance"] == distance and t["direction"] == direction and key in t["metrics"]]
            if group:
                ax.plot(*zip(*group), marker=".", label=f"{distance:g} {'CW' if direction == 1 else 'CCW'}")
        ax.set(xlabel="Trial number", ylabel=label); ax.grid(alpha=.25)
        changes = [i+1 for i in range(1, len(trials)) if trials[i]["settings"] != trials[i-1]["settings"]]
        for change in changes: ax.axvline(change-.5, color="gray", alpha=.25, lw=.7)
    axes.flat[0].axhline(.15, color="gray", ls=":")
    for y in (PULSE_CM, -PULSE_CM): axes.flat[1].axhline(y, color="gray", ls=":")
    axes.flat[0].legend(fontsize=7, ncol=3)
    fig.suptitle("Measured tuning progress — compare matching distance and direction")
    fig.tight_layout(rect=(0, 0, 1, .96)); fig.savefig(root/"tuning_progress.png", dpi=140); plt.close(fig)
    validation = [t for t in trials if t["stage"] == "validation"]
    frozen_settings = validation[-1]["settings"] if validation else None
    final = [t for t in good if t["stage"] == "validation" and t["settings"] == frozen_settings]
    final_count = sum(t["settings"] == frozen_settings for t in validation)
    final_label = f"Validation of shared configuration — {final_count}/54 trials recorded"
    if final:
        final_metrics = metrics[:3]+[("peak_velocity_error_cm_s", "Peak velocity error (cm/s)"),
                                   ("duration_s", "Trial duration (s)"), ("restart_kicks", "Restart kicks (circles) / stops (crosses)")]
        fig, axes = plt.subplots(3, 2, figsize=(13, 11))
        for ax, (key, label) in zip(axes.flat, final_metrics):
            for direction, color in ((1, "tab:blue"), (-1, "tab:orange")):
                ds = sorted(set(t["distance"] for t in final if t["direction"] == direction))
                if not ds: continue
                means=[]; low=[]; high=[]
                for d in ds:
                    values=[t["metrics"][key] for t in final if t["distance"] == d and t["direction"] == direction]
                    mean=float(np.mean(values)); means.append(mean); low.append(mean-min(values)); high.append(max(values)-mean)
                    ax.scatter([d]*len(values), values, color=color, alpha=.4, s=18)
                    if key == "restart_kicks":
                        ax.scatter([d]*len(values), [t["metrics"]["intermediate_stops"] for t in final if t["distance"] == d and t["direction"] == direction], marker="x", color=color)
                ax.errorbar(ds, means, yerr=[low, high], fmt="o-", color=color, capsize=4, label="CW" if direction == 1 else "CCW")
            ax.set(xlabel="Commanded distance (cm)", ylabel=label); ax.set_xscale("log"); ax.grid(alpha=.25); ax.legend()
        axes.flat[0].axhline(.15, color="gray", ls=":")
        for y in (PULSE_CM, -PULSE_CM): axes.flat[1].axhline(y, color="gray", ls=":")
        fig.suptitle(final_label + "; mean and observed range")
        fig.tight_layout(rect=(0, 0, 1, .96)); fig.savefig(root/"final_metrics.png", dpi=150); plt.close(fig)
        distances = sorted(set(t["distance"] for t in final), reverse=True)
        fig, axes = plt.subplots(len(distances), 2, figsize=(12, 2.5*len(distances)), squeeze=False)
        for i, distance in enumerate(distances):
            for j, direction in enumerate((1, -1)):
                ax = axes[i, j]
                rows = []
                for trial in final:
                    if trial["distance"] != distance or trial["direction"] != direction: continue
                    with (root/trial["id"]/"data.csv").open() as stream: rows = list(csv.DictReader(stream))
                    ax.plot([float(r["time_s"]) for r in rows], [direction*float(r["velocity_cm_s"]) for r in rows], alpha=.65, label=f"Actual r{trial['repeat']}")
                if rows:
                    ax.plot([float(r["time_s"]) for r in rows], [direction*float(r["ref_velocity_cm_s"]) for r in rows], "k--", label="Reference")
                ax.set(title=f"{distance:g} cm {'CW' if direction == 1 else 'CCW'}", xlabel="Time (s)", ylabel="Speed (cm/s)")
                ax.grid(alpha=.25)
                if rows: ax.legend(fontsize=7)
                else: ax.text(.5, .5, "No measured validation data yet", ha="center", transform=ax.transAxes)
        fig.tight_layout(); fig.savefig(root/"final_velocity_profiles.png", dpi=140); plt.close(fig)
    # Matched baseline/final anchor comparison.
    anchors = [d for d in (500, 100, 10) if any(t["stage"] == "baseline" and t["distance"] == d for t in good)]
    if anchors and final:
        fig, axes = plt.subplots(len(anchors), 1, figsize=(11, 3*len(anchors)), squeeze=False)
        for ax, distance in zip(axes.flat, anchors):
            for stage, style in (("baseline", ":"), ("validation", "-")):
                candidates = final if stage == "validation" else good
                candidate = next((t for t in candidates if t["stage"] == stage and t["distance"] == distance and t["direction"] == 1), None)
                if candidate:
                    with (root/candidate["id"]/"data.csv").open() as stream: rows=list(csv.DictReader(stream))
                    ax.plot([float(r["time_s"]) for r in rows], [float(r["velocity_cm_s"]) for r in rows], style, label=stage)
                    if stage == "validation": ax.plot([float(r["time_s"]) for r in rows], [float(r["ref_velocity_cm_s"]) for r in rows], "k--", label="Reference")
            ax.set(title=f"{distance} cm CW: baseline vs final", xlabel="Time (s)", ylabel="Velocity (cm/s)"); ax.legend(); ax.grid(alpha=.25)
        fig.tight_layout(); fig.savefig(root/"baseline_vs_final.png", dpi=140); plt.close(fig)
    with (root/"summary.csv").open("w", newline="") as stream:
        keys = ["id", "stage", "distance", "direction", "repeat", "outcome", "screen_pass", "screen_reasons"]+[k for k, _ in metrics]+["peak_velocity_error_cm_s"]
        writer=csv.DictWriter(stream, fieldnames=keys, extrasaction="ignore"); writer.writeheader()
        writer.writerows(dict(dict(t, **t["metrics"]), screen_reasons="; ".join(t["metrics"].get("screen_reasons", []))) for t in trials)
    parts=["<!doctype html><meta charset='utf-8'><title>Distance-control tuning</title><style>body{font-family:system-ui;margin:32px;max-width:1400px}img{max-width:100%}table{border-collapse:collapse}th,td{border:1px solid #ddd;padding:6px}tr.fail{background:#ffe6df}tr.pass{background:#e7f5e8}</style>",
           "<h1>Unloaded distance-control tuning</h1><p>Measured COM3 shaft trials. Position tolerance: one pulse (0.628 cm). Green indicates numerical screening; graph review is also required. Loaded travel has not been tested.</p>",
           "<p><a href='summary.csv'>Metric table (CSV)</a></p>"]
    if frozen_settings is not None:
        parts.append(f"<p>{html.escape(final_label)}. Charts include only the latest validation settings; unavailable metrics stay in the failed-trial table.</p><pre>{html.escape(json.dumps(frozen_settings, indent=2))}</pre>")
    for name in ("final_metrics", "final_velocity_profiles", "baseline_vs_final", "tuning_progress"):
        if (root/f"{name}.png").exists(): parts.append(f"<h2>{html.escape(name.replace('_',' ').title())}</h2><img src='{name}.png'>")
    parts.append("<h2>Every trial</h2><table><tr><th>Trial / trace</th><th>Outcome</th><th>Final error cm</th><th>Velocity RMSE %</th><th>Stops / kicks</th><th>Settings / CSV</th></tr>")
    for trial in trials:
        m=trial["metrics"]; tid=trial["id"]
        fmt=lambda key: f"{m[key]:.3f}" if m.get(key) is not None else "—"
        percent=f"{100*m['normalised_rmse']:.1f}" if m.get("normalised_rmse") is not None else "—"
        reason=html.escape("; ".join(m.get("screen_reasons", [])))
        parts.append(f"<tr class={'pass' if m['screen_pass'] else 'fail'}><td><a href='{tid}/trace.png'>{html.escape(tid)}</a> <a href='{tid}/pulses.png'>pulses</a></td><td>{html.escape(m['outcome'])}<br><small>{reason}</small></td><td>{fmt('final_error_cm')}</td><td>{percent}</td><td>{m.get('intermediate_stops','—')} / {m.get('restart_kicks','—')}</td><td><a href='{tid}/metadata.json'>settings</a> <a href='{tid}/data.csv'>CSV</a> <a href='{tid}/serial.log'>raw log</a></td></tr>")
    parts.append("</table>"); (root/"index.html").write_text("\n".join(parts), encoding="utf-8")


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("action", choices=("run", "report"))
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--distances", nargs="+", type=float, default=[500])
    parser.add_argument("--directions", nargs="+", type=int, default=[1])
    parser.add_argument("--settings", type=Path)
    parser.add_argument("--stage", default="tuning")
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--output", type=Path, default=ROOT)
    parser.add_argument("--fault-no-heartbeat", action="store_true", help="Explicitly test firmware host-loss shutdown")
    args=parser.parse_args()
    if args.action == "report": report(args.output); return
    settings=dict(DEFAULTS)
    if args.settings: settings.update(json.loads(args.settings.read_text()))
    for distance in args.distances:
        for direction in args.directions:
            for repeat in range(1, args.repeats+1):
                metadata=run_trial(args.port, distance, direction, settings, args.output, args.stage, repeat,
                                   heartbeat=not args.fault_no_heartbeat)
                print(json.dumps(dict(id=metadata["id"], **metadata["metrics"])), flush=True)
                if args.stage != "validation": report(args.output)
                if metadata["metrics"]["outcome"] in ("HOST_ERROR", "INTERRUPTED", "EXCESS_TRAVEL", "HOST_TIMEOUT", "HOST_DEADLINE"):
                    if args.stage == "validation": report(args.output)
                    raise SystemExit("Trial stopped; inspect captured failure before resuming")
        if args.stage == "validation": report(args.output)


if __name__ == "__main__": main()
