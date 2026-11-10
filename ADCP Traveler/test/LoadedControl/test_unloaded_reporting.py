"""Pin direction normalization and the production telemetry phase vocabulary."""
import unittest
import plot_unloaded

class ReportingTests(unittest.TestCase):
    def test_reverse_tracking_uses_signed_request_and_travelling_phase(self):
        rows=[dict(phase='TRAVELLING',command_cm_s=-10,velocity_cm_s=-11),
              dict(phase='APPROACHING',command_cm_s=-2,velocity_cm_s=-1),
              dict(phase='STARTING',command_cm_s=0,velocity_cm_s=-50)]
        rmse,zeros=plot_unloaded.tracking_metrics(rows,-1)
        self.assertEqual(rmse,1)
        self.assertEqual(zeros,0)

    def test_stationary_requested_travel_counts_but_idle_does_not(self):
        rows=[dict(phase='TRAVELLING',command_cm_s=2,velocity_cm_s=0),
              dict(phase='IDLE',command_cm_s=0,velocity_cm_s=0)]
        self.assertEqual(plot_unloaded.tracking_metrics(rows,1),(2,1))

if __name__=='__main__':unittest.main()
