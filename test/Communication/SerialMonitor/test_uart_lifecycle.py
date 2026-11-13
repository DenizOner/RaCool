import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from Communication.SerialMonitor.diagnostic.verify_uart_lifecycle import status, idle_for_test


class UartStatusEvidenceTest(unittest.TestCase):
    def test_foreign_uart_and_pending_work_are_not_admitted(self):
        self.assertTrue(idle_for_test((2,0,0,0,0)))
        for state in ((2,1,1,0,0),(2,0,0,1,0),(2,0,0,0,1),(3,0,0,0,0)):
            self.assertFalse(idle_for_test(state))
    def test_only_complete_fresh_status_is_parseable(self):
        self.assertIsNone(status('SER_PERF uart-start\nUART started; ownership and worker state verified.'))
        self.assertIsNone(status('SerialMonitor State: 2; UART Owned: 1'))
        self.assertEqual((2, 1, 1, 0, 0), status('RACOOL_CLI v=1 type=serial_monitor state=2 owned=1 active=1 closing=0 pending_tx=0 queued_bytes=0 tx_bytes=0 rx_bytes=0 rejected=0 dropped=0 tx_failures=0 rx_failures=0\n'))

    def test_latest_invalid_record_cannot_borrow_previous_fields(self):
        valid = 'RACOOL_CLI v=1 type=serial_monitor state=2 owned=1 active=1 closing=0 pending_tx=0 queued_bytes=0 tx_bytes=0 rx_bytes=0 rejected=0 dropped=0 tx_failures=0 rx_failures=0\n'
        self.assertIsNone(status(valid + 'RACOOL_CLI v=2 type=serial_monitor state=2\n'))
        self.assertIsNone(status(valid + valid.rstrip()))
        self.assertIsNone(status(valid.replace('owned=1', 'owned=2')))
        self.assertIsNone(status(valid.replace('pending_tx=0', 'pending_tx=4294967296')))


if __name__ == '__main__':
    unittest.main()
