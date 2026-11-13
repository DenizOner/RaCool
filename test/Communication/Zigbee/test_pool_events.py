import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / 'diagnostic'))
from analyze_pool_events import analyze


class PoolEventTests(unittest.TestCase):
    def test_allocations_and_unmatched_free(self):
        result = analyze('RPTEVENT: window=0:5\nRPTEVENT: data=0:M:10:0,1:A:10:1234,2:M:20:0,3:F:10:0,4:F:30:0,\n')[0]
        self.assertEqual(result['unfreed_in_window'], {'unknown': 1})
        self.assertEqual(result['counts']['unmatched_free'], 1)

    def test_rejects_missing_duplicate_gap_and_absent(self):
        for text in ('', 'RPTEVENT: window=0:2\nRPTEVENT: data=0:M:10:0,',
                     'RPTEVENT: window=0:1\nRPTEVENT: data=0:G:0:0,',
                     'RPTEVENT: window=0:1\nRPTEVENT: data=0:M:10:0,0:M:10:0,'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                analyze(text)

    def test_separate_boot_snapshots(self):
        self.assertEqual(len(analyze('RPTEVENT: window=0:1\nRPTEVENT: data=0:X:0:0,\nRPTEVENT: window=0:1\nRPTEVENT: data=0:Z:0:1234,')), 2)

    def test_accepts_colored_serial_log(self):
        self.assertEqual(analyze('\x1b[0;32mI RPTEVENT: window=0:1\x1b[0m\n\x1b[0;32mI RPTEVENT: data=0:X:0:0,\x1b[0m')[0]['counts']['X'], 1)

    def test_buffer_pool_events(self):
        result = analyze('RPTEVENT: window=0:4\nRPTEVENT: data=0:B:12:0,1:C:0:0,2:D:12:0,3:D:13:0,')[0]
        self.assertEqual(result['counts']['C'], 1)
        self.assertEqual(result['counts']['unmatched_buffer_free'], 1)
        self.assertEqual(result['unfreed_buffers_in_window'], 0)


if __name__ == '__main__':
    unittest.main()
