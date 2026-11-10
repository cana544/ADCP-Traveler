"""Capture diagnostic firmware without issuing motion commands; analyse marked-rope measurements."""
import argparse
import json
import math
import time
from pathlib import Path

REQUIRED = {"version", "boot", "us", "move", "phase", "position_cm", "target_cm",
            "applied_v", "drops"}
TERMINAL = {"COMPLETE", "ERROR", "STOPPED"}

def _finite(value):
    if isinstance(value, float) and not math.isfinite(value):
        raise ValueError("nonfinite diagnostic value")
    if isinstance(value, dict):
        for item in value.values(): _finite(item)
    elif isinstance(value, list):
        for item in value: _finite(item)

def parse_line(line):
    if not line.startswith("GG,"): return None
    if len(line) > 8192: raise ValueError("oversized diagnostic record")
    try:
        record = json.loads(line[3:])
    except (json.JSONDecodeError, TypeError) as exc:
        raise ValueError("malformed diagnostic JSON") from exc
    if not isinstance(record, dict) or not REQUIRED <= record.keys() or record["version"] != 1:
        raise ValueError("unsupported or incomplete diagnostic record")
    _finite(record)
    for key in ("boot", "us", "move", "drops"):
        if type(record[key]) is not int or record[key] < 0: raise ValueError("invalid identity/counter")
    for key in ("seq", "recoveries"):
        if key in record and (type(record[key]) is not int or record[key] < 0):
            raise ValueError("invalid optional counter")
    for key in ("mode", "failure"):
        if key in record and not isinstance(record[key], str):
            raise ValueError("invalid optional text field")
    for key in ("position_cm", "target_cm", "applied_v"):
        if type(record[key]) not in (int, float): raise ValueError("invalid numeric field")
    if "command_cm_s" in record and type(record["command_cm_s"]) not in (int, float):
        raise ValueError("invalid command velocity")
    if not isinstance(record["phase"], str): raise ValueError("invalid phase")
    return record

def measurement_error(requested_cm, actual_cm, encoder_cm):
    # Distances are magnitudes along the commanded direction, both directions alike.
    if not all(math.isfinite(v) for v in (requested_cm, actual_cm, encoder_cm)) or requested_cm <= 0:
        raise ValueError("measurements must be finite and request must be positive")
    error = actual_cm - requested_cm
    return {"requested_cm": requested_cm, "actual_cm": actual_cm, "encoder_cm": encoder_cm,
            "physical_error_cm": error, "encoder_error_cm": encoder_cm-requested_cm,
            "wheel_minus_body_cm": encoder_cm-actual_cm,
            "within_1cm": abs(error) <= 1.0, "overshoot": error > 0,
            "one_sided_pass": -1.0 <= error <= 0}

def summarize(rows):
    runs = {}
    invalidated = set()
    for row in rows:
        if row["move"] == 0: continue
        key = (row["boot"], row["move"])
        if row.get("mode") in {"MANUAL", "SCAN"}:
            invalidated.add(key)
        # Later IDLE snapshots still carry the last automatic move's phase/ID.
        # Do not attribute unrelated displacement to a missing terminal sample.
        if key in invalidated: continue
        if key not in runs:
            runs[key] = dict(boot=key[0], move=key[1], samples=0, terminal=False,
                             phase=row["phase"], max_drops=0, peak_applied_v=0, recoveries=0,
                             sequence_gaps=0, _last_seq=None)
        run = runs[key]
        run["samples"] += 1
        run["max_drops"] = max(run["max_drops"], row["drops"])
        run["peak_applied_v"] = max(run["peak_applied_v"], abs(row["applied_v"]))
        run["recoveries"] = max(run["recoveries"], row.get("recoveries", 0))
        sequence = row.get("seq")
        if sequence is not None and run["_last_seq"] is not None:
            gap = (sequence-run["_last_seq"]) & 0xFFFFFFFF
            if gap > 1: run["sequence_gaps"] += gap-1
        run["_last_seq"] = sequence
        if not run["terminal"]:
            run["phase"] = row["phase"]
            run["encoder_endpoint_cm"] = row["position_cm"]
            run["target_cm"] = row["target_cm"]
            run["failure"] = row.get("failure", "")
            run["terminal"] = row["phase"] in TERMINAL
    for run in runs.values(): del run["_last_seq"]
    return list(runs.values())

def capture(args):
    import serial
    output = Path(args.output)
    if output.exists(): raise ValueError("output already exists; choose a new filename")
    if not 0 < args.seconds <= 3600: raise ValueError("capture duration must be 0–3600 seconds")
    output.parent.mkdir(parents=True, exist_ok=True)
    # Creating/closing this port may reboot an ESP32; begin with the traveller stopped.
    with serial.Serial(args.port, 115200, timeout=.2, write_timeout=.5) as port, output.open("x", encoding="utf-8") as log:
        time.sleep(2)
        port.write(b"LOG ON\n")
        end = time.monotonic() + args.seconds
        print("Recording. Operate with the existing app. No motion command is sent by this tool.")
        try:
            while time.monotonic() < end:
                raw = port.read_until(b"\n", 8192)
                if raw:
                    log.write(raw.decode("utf-8", errors="replace")); log.flush()
        finally:
            port.write(b"STOP\nLOG OFF\n")
    print(f"Saved {output}; STOP requested when capture ended.")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    command = sub.add_parser("capture")
    command.add_argument("--port", required=True)
    command.add_argument("--seconds", type=float, default=120)
    command.add_argument("--output", required=True)
    command = sub.add_parser("report")
    command.add_argument("log")
    command = sub.add_parser("measure")
    command.add_argument("--requested-cm", type=float, required=True)
    command.add_argument("--actual-cm", type=float, required=True)
    command.add_argument("--encoder-cm", type=float, required=True)
    args = parser.parse_args()
    if args.action == "capture": capture(args)
    elif args.action == "measure":
        print(json.dumps(measurement_error(args.requested_cm, args.actual_cm, args.encoder_cm), indent=2))
    else:
        rows, rejected = [], 0
        for line in Path(args.log).read_text(encoding="utf-8").splitlines():
            try:
                row = parse_line(line)
                if row is not None: rows.append(row)
            except ValueError: rejected += 1
        print(json.dumps(dict(rejected_records=rejected, runs=summarize(rows)), indent=2))

if __name__ == "__main__": main()
