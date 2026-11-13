"""Serial capture must preserve UTF-8 across arbitrary UART read boundaries."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))


import runpy
import tempfile
import types
import unittest
from unittest.mock import patch


class SerialCaptureTests(unittest.TestCase):
    def test_utf8_split_into_single_byte_reads(self):
        payload = 'Mm ğü ╭──┐\n'.encode('utf-8')
        clock = [0.0]

        class FakeSerial:
            def __init__(self, **kwargs):
                self.offset = 0
                self.in_waiting = 1
            def __enter__(self):
                return self
            def __exit__(self, *args):
                return False
            def read(self, count):
                clock[0] += 0.05
                result = payload[self.offset:self.offset + 1]
                self.offset += 1
                return result

        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / 'capture.log'
            args = ['serial_capture.py', '--port', 'TEST', '--duration', '2', '--output', str(target)]
            def sleep(seconds):
                clock[0] += seconds
            modules = {'serial': types.SimpleNamespace(Serial=FakeSerial),
                       'time': types.SimpleNamespace(monotonic=lambda: clock[0], sleep=sleep)}
            # run_path does not add the script directory as normal CLI execution does.
            helper_directory = str(Path(__file__).resolve().parents[2] / 'Communication/DebugConsole/diagnostic')
            with patch.object(sys, 'path', [helper_directory, *sys.path]), \
                    patch.object(sys, 'argv', args), patch.dict(sys.modules, modules):
                runpy.run_path(str(Path(__file__).resolve().parents[2] / 'Communication/DebugConsole/diagnostic' / 'serial_capture.py'), run_name='__main__')
            self.assertEqual(payload.decode('utf-8'), target.read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
