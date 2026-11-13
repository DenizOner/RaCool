"""Acceptance parser must distinguish actual boot workers from acknowledgments."""
import unittest
from diagnostic.verify_led_startup import parse_workers, parse_driver_error


class StartupEvidenceTests(unittest.TestCase):
    def test_missing_or_failed_driver_is_not_success(self):
        self.assertIsNone(parse_driver_error('RACOOL_CLI v=1 type=builtin_status on=0 enabled=1 brightness=255 animation=-1 priority=0 remaining_ms=0 plugins=21 driver_error=0'))
        self.assertEqual(parse_driver_error('RACOOL_CLI v=1 type=builtin_status on=0 enabled=1 brightness=255 animation=-1 priority=0 remaining_ms=0 plugins=21 driver_error=0\n'),0)
        self.assertEqual(parse_driver_error('RACOOL_CLI v=1 type=builtin_status on=0 enabled=1 brightness=255 animation=-1 priority=0 remaining_ms=0 plugins=21 driver_error=261\n'),0x105)
    def test_complete_signed_worker_rows_and_boot_phase(self):
        self.assertEqual(parse_workers('RACOOL_CLI v=1 type=builtin_status on=0 enabled=1 brightness=255 animation=-1 priority=0 remaining_ms=0 plugins=21 driver_error=0\nRACOOL_CLI v=1 type=rgb_status brightness=255 mode=3 target_r=0 target_g=0 target_b=0 priority=1 animation=4 on=1 fading=0 current_r=0 current_g=0 current_b=0\r\n'
                         'RACOOL_CLI v=1 type=ui screen=active service=2 boot=running\r\n'), (-1,4,'running'))

    def test_partial_or_acknowledgment_is_not_actuation(self):
        self.assertIsNone(parse_workers('RACOOL_CLI v=1 type=builtin_boot animation=17\n'))
        self.assertIsNone(parse_workers('RACOOL_CLI v=1 type=builtin_status on=0 enabled=1 brightness=255 animation=17 priority=0 remaining_ms=0 plugins=21 driver_error=0\nRACOOL_CLI v=1 type=rgb_status brightness=255 mode=3 target_r=0 target_g=0 target_b=0 priority=1 animation=4 on=1 fading=0 current_r=0 current_g=0 current_b=0\n'
                         'RACOOL_CLI v=1 type=ui screen=active service=2 boot=running'))

    def test_finished_boot_cannot_be_read_as_running(self):
        self.assertEqual(parse_workers('RACOOL_CLI v=1 type=builtin_status on=0 enabled=1 brightness=255 animation=17 priority=0 remaining_ms=0 plugins=21 driver_error=0\nRACOOL_CLI v=1 type=rgb_status brightness=255 mode=3 target_r=0 target_g=0 target_b=0 priority=1 animation=4 on=1 fading=0 current_r=0 current_g=0 current_b=0\n'
                         'RACOOL_CLI v=1 type=ui screen=active service=2 boot=running\n'
                         'RACOOL_CLI v=1 type=ui screen=active service=2 boot=idle\n'), (17,4,'idle'))


if __name__ == '__main__':
    unittest.main()
