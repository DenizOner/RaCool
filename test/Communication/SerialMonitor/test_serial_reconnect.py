import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import unittest
from Infrastructure.diagnostic.serial_reconnect import read_available


class Port:
    in_waiting = 1
    def __init__(self, failures=0):
        self.failures = failures
        self.opens = self.closes = 0
    def read(self, count):
        raise OSError("device disconnected")
    def close(self):
        self.closes += 1
    def open(self):
        self.opens += 1
        if self.opens <= self.failures:
            raise OSError("not enumerated")


class ReconnectTests(unittest.TestCase):
    def test_healthy_read_does_not_reopen(self):
        port = Port()
        port.read = lambda count: b"response"
        events = []
        self.assertEqual(read_available(port, 1, events.append), b"response")
        self.assertEqual((port.opens, port.closes), (0, 0))
        self.assertEqual(events, [])

    def test_retries_open_without_replaying_commands(self):
        port = Port(2)
        events = []
        ticks = [0.0]
        result = read_available(port, 2, events.append,
                                clock=lambda: ticks[0],
                                sleep=lambda delta: ticks.__setitem__(0, ticks[0] + delta))
        self.assertEqual(result, b"")
        self.assertEqual((port.opens, port.closes), (3, 1))
        self.assertEqual(len(events), 2)

    def test_missing_device_fails_with_bounded_wait(self):
        ticks = [0.0]
        port = Port(100)
        with self.assertRaises(TimeoutError):
            read_available(port, 0.5, lambda _: None,
                           clock=lambda: ticks[0],
                           sleep=lambda delta: ticks.__setitem__(0, ticks[0] + delta))
        self.assertEqual(ticks[0], 0.5)
        self.assertEqual(port.opens, 2)


if __name__ == "__main__":
    unittest.main()
