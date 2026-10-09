import unittest
import math
import numpy as np
import tempfile
import json
import csv
from pathlib import Path
from unittest.mock import patch
from tuning import profile_duration, analyse, parse_sample, run_trial, report, DEFAULTS, FIELDS


class TuningTests(unittest.TestCase):
    def test_profile_duration(self):
        self.assertAlmostEqual(profile_duration(10, 20, 10), 2)
        self.assertAlmostEqual(profile_duration(40, 20, 10), 4)
        self.assertAlmostEqual(profile_duration(500, 20, 10), 27)

    def test_nonfinite_telemetry_rejected(self):
        with self.assertRaises(ValueError):
            parse_sample("0.05,nan,1,2,40,1,1,0,0,MOVING,100,200,1")

    def test_exact_tracking_metrics(self):
        rows = []
        for t in np.arange(0.05, 3.05, .05):
            v = 10*t if t < 1 else max(0, 10*(2-t))
            x = 5*t*t if t < 1 else 10-5*max(0, 2-t)**2
            rows.append(dict(time_s=t, distance_cm=x, velocity_cm_s=v,
                             ref_velocity_cm_s=v, ref_position_cm=x,
                             velocity_cmd_cm_s=v, effort_v=2 if t < 2 else 0,
                             pwm=40 if t < 2 else 0, state="MOVING" if t < 2 else "COMPLETE",
                             position_error_cm=0, pulse_age_us=0, pulse_period_us=60000, pulses=int(x/.628)))
        result = analyse(rows, 10, 1, "COMPLETE", dict(vmax=20, amax=10))
        self.assertAlmostEqual(result["final_error_cm"], 0)
        self.assertAlmostEqual(result["velocity_rmse_cm_s"], 0)
        self.assertEqual(result["restart_kicks"], 0)
        self.assertTrue(result["screen_pass"])

    def test_timeout_not_counted_as_success(self):
        result = analyse([], 10, 1, "HOST_TIMEOUT", dict(vmax=20, amax=10))
        self.assertFalse(result["screen_pass"])
        self.assertEqual(result["outcome"], "HOST_TIMEOUT")

    def test_serial_failure_saved_as_failed_trial(self):
        test_root = Path(__file__).resolve().parent
        with tempfile.TemporaryDirectory(dir=test_root) as folder:
            self.assertIn(test_root, Path(folder).resolve().parents)
            with patch("tuning.serial.Serial", side_effect=PermissionError("port busy")):
                trial = run_trial("COM3", 10, 1, DEFAULTS, folder)
            metadata = json.loads((Path(folder)/trial["id"]/"metadata.json").read_text())
            self.assertEqual(metadata["metrics"]["outcome"], "HOST_ERROR")
            self.assertFalse(metadata["metrics"]["screen_pass"])
            self.assertTrue((Path(folder)/trial["id"]/"data.csv").exists())

    def test_first_pulse_latency_is_not_an_intermediate_stop(self):
        rows = []
        for t, x, v in ((.25, .6283, 0), (.35, 1.2566, 5), (2.0, 10, 0), (3.0, 10, 0)):
            rows.append(dict(time_s=t, distance_cm=x, velocity_cm_s=v,
                ref_velocity_cm_s=10*t if t < 1 else 0, ref_position_cm=x,
                velocity_cmd_cm_s=v, effort_v=2 if t < 2 else 0,
                pwm=40 if t < 2 else 0, state="MOVING" if t < 2 else "SETTLING",
                position_error_cm=0, pulse_age_us=0, pulse_period_us=100000, pulses=int(x/.628)))
        # A first pulse carries position but has no complete pulse period.
        rows[0]["ref_velocity_cm_s"] = 3.1
        result=analyse(rows, 10, 1, "COMPLETE", dict(vmax=20, amax=10))
        self.assertEqual(result["intermediate_stops"], 0)

    def test_settling_only_failure_metrics_are_serialisable(self):
        row = dict(time_s=3, distance_cm=5, velocity_cm_s=0,
            ref_velocity_cm_s=0, ref_position_cm=10, velocity_cmd_cm_s=0,
            effort_v=0, pwm=0, state="SETTLING", position_error_cm=5,
            pulse_age_us=600000, pulse_period_us=100000, pulses=8)
        result=analyse([row], 10, 1, "HOST_ERROR", dict(vmax=20, amax=10))
        json.dumps(result, allow_nan=False)
        self.assertFalse(result["screen_pass"])

    def test_malformed_timestamp_stops_capture_immediately(self):
        class FakePort:
            def __init__(self):
                self.lines = iter([b"DC_EVENT,READY,V2\n", b"DC_ACK,CONFIG\n",
                    b"DC_EVENT,START,10,1,TRIANGULAR,2\n",
                    b"nan,0,0,0,0,0,0,0,0,MOVING,0,0,0\n",
                    b"DC_EVENT,END,COMPLETE,0\n"])
                self.writes=[]; self.reads=0
            def write(self, value): self.writes.append(value)
            def readline(self): self.reads+=1; return next(self.lines, b"")
            def close(self): pass
        port=FakePort()
        test_root=Path(__file__).resolve().parent
        with tempfile.TemporaryDirectory(dir=test_root) as folder:
            self.assertIn(test_root, Path(folder).resolve().parents)
            with patch("tuning.serial.Serial", return_value=port), patch("tuning.time.sleep"):
                trial=run_trial("COM3", 10, 1, DEFAULTS, folder)
            self.assertEqual(trial["metrics"]["outcome"], "HOST_ERROR")
            self.assertEqual(port.reads, 4)
            self.assertGreaterEqual(port.writes.count(b"STOP\n"), 2)

    def test_report_preserves_settling_only_validation_failure(self):
        test_root=Path(__file__).resolve().parent
        with tempfile.TemporaryDirectory(dir=test_root) as folder:
            self.assertIn(test_root, Path(folder).resolve().parents)
            for number, t, outcome in ((1, .5, "COMPLETE"), (2, 3, "HOST_ERROR")):
                tid=f"trial_{number:03d}_test"
                target=Path(folder)/tid; target.mkdir()
                row=dict(time_s=t, distance_cm=1.25 if t < 2 else 5,
                    velocity_cm_s=5 if t < 2 else 0, ref_velocity_cm_s=5 if t < 2 else 0,
                    ref_position_cm=1.25 if t < 2 else 10, velocity_cmd_cm_s=5 if t < 2 else 0,
                    effort_v=2 if t < 2 else 0, pwm=40 if t < 2 else 0,
                    state="MOVING" if t < 2 else "SETTLING", position_error_cm=0,
                    pulse_age_us=0, pulse_period_us=100000, pulses=2)
                with (target/"data.csv").open("w", newline="") as stream:
                    writer=csv.DictWriter(stream, fieldnames=FIELDS); writer.writeheader(); writer.writerow(row)
                metadata=dict(id=tid, distance=10, direction=1, stage="validation", repeat=number,
                              settings=DEFAULTS, metrics=analyse([row], 10, 1, outcome, DEFAULTS))
                (target/"metadata.json").write_text(json.dumps(metadata, allow_nan=False))
            report(folder)
            self.assertIn("HOST_ERROR", (Path(folder)/"index.html").read_text(encoding="utf-8"))
            self.assertTrue((Path(folder)/"final_metrics.png").exists())


if __name__ == "__main__":
    unittest.main()
