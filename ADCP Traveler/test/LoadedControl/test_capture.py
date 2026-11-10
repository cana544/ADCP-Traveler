import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("capture_loaded", Path(__file__).resolve().parents[2] / "scripts/capture_loaded.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)

class CaptureTests(unittest.TestCase):
    def sample(self):
        return dict(version=1, boot=1, us=1, move=1, phase="COMPLETE", position_cm=99.3,
                    target_cm=100, command_cm_s=0, applied_v=0, drops=0)

    def test_bad_and_nonfinite_records(self):
        self.assertIsNone(capture.parse_line("[MOTOR] log"))
        self.assertEqual(capture.parse_line("GG," + json.dumps(self.sample()))["move"], 1)
        for text in ["GG,{}", "GG,{", "GG," + json.dumps(dict(self.sample(), position_cm=float("nan")))]:
            with self.assertRaises(ValueError): capture.parse_line(text)

    def test_physical_error_is_not_encoder_error(self):
        summary = capture.measurement_error(100, 99.5, 99.3)
        self.assertAlmostEqual(summary["physical_error_cm"], -.5)
        self.assertAlmostEqual(summary["encoder_error_cm"], -.7)
        self.assertTrue(summary["within_1cm"])
        self.assertFalse(summary["overshoot"])
        self.assertTrue(capture.measurement_error(100, 100.1, 99.3)["overshoot"])
        with self.assertRaises(ValueError): capture.measurement_error(100, float("inf"), 100)

    def test_run_identity_and_incomplete_records(self):
        rows = [self.sample(), dict(self.sample(), boot=2, phase="MOVING")]
        report = capture.summarize(rows)
        self.assertEqual(len(report), 2)
        self.assertTrue(report[0]["terminal"])
        self.assertFalse(report[1]["terminal"])
        self.assertEqual(capture.summarize([]), [])

    def test_manual_samples_do_not_change_automatic_run_metrics(self):
        rows = [self.sample(), dict(self.sample(), mode="MANUAL", applied_v=9),
                dict(self.sample(), mode="SCAN", applied_v=12)]
        report = capture.summarize(rows)
        self.assertEqual(report[0]["samples"], 1)
        self.assertEqual(report[0]["peak_applied_v"], 0)

    def test_optional_fields_cannot_crash_reporting(self):
        for fields in [dict(seq="bad"), dict(recoveries=None), dict(mode=3), dict(failure=[]), dict(seq=-1)]:
            with self.assertRaises(ValueError):
                capture.parse_line("GG," + json.dumps(dict(self.sample(), **fields)))

    def test_manual_motion_invalidates_late_endpoint_attribution(self):
        rows = [dict(self.sample(), mode="DISTANCE", phase="MOVING", position_cm=50),
                dict(self.sample(), mode="MANUAL", position_cm=150),
                dict(self.sample(), mode="IDLE", position_cm=200),
                dict(self.sample(), mode="DISTANCE", move=2, position_cm=299.3)]
        report = capture.summarize(rows)
        self.assertFalse(report[0]["terminal"])
        self.assertEqual(report[0]["encoder_endpoint_cm"], 50)
        self.assertTrue(report[1]["terminal"])

    def test_baseline_keeps_original_controller_unchanged(self):
        import subprocess
        path = Path(__file__).resolve().parents[2] / "scripts/prepare_baseline.py"
        module_spec = importlib.util.spec_from_file_location("prepare_baseline", path)
        baseline = importlib.util.module_from_spec(module_spec)
        module_spec.loader.exec_module(baseline)
        with tempfile.TemporaryDirectory() as folder:
            destination = Path(folder) / "baseline"
            baseline.prepare(destination)
            original = subprocess.check_output(["git", "show", baseline.BASE + ":ADCP Traveler/src/distance_controller.cpp"], cwd=baseline.REPO)
            self.assertEqual((destination / "src/distance_controller.cpp").read_bytes(), original)
            with self.assertRaises(FileExistsError): baseline.prepare(destination)

if __name__ == "__main__": unittest.main()
