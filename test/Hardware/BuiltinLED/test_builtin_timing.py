import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import importlib.util
import unittest

spec = importlib.util.spec_from_file_location("capture", Path(__file__).resolve().parents[2] / "Hardware/BuiltinLED/diagnostic/capture_builtin_timing.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)

class TimingEvidenceTests(unittest.TestCase):
    def test_one_hundred_complete_cycles(self):
        lines = []
        for cycle in range(102):
            for phase, value in ((0, 255), (80, 0), (200, 255), (280, 0)):
                t = (cycle * 2000 + phase + 1) * 1000
                lines.append(f"LEDTRACE row t={t} output={t+10} advance={t//1000} dt=20 lock=0 hw=10 gen=1 anim=14 cmd=-1 rgb={value},{value},{value}")
        text = f"LEDTRACE summary rows={len(lines)} dropped=0\n" + "\n".join(lines)
        result = capture.analyze(text)
        self.assertEqual(100, result["double_blink"]["complete_cycles"])
        self.assertEqual(2000, result["double_blink"]["period_mean_ms"])
        self.assertEqual(-1, result["rows"][0]["cmd"])
        self.assertFalse(result["physical_output_verified"])
    def test_missing_pulse_is_counted_not_hidden_by_return_to_same_color(self):
        rows = [{"advance": t, "t": t*1000, "rgb": (value,)*3, "gen": 1, "cmd": -1}
                for t, value in ((0, 255), (80, 0), (280, 0), (2000, 255), (2080, 0))]
        result = capture.phase_metrics(rows)
        self.assertTrue(result["available"])
        self.assertEqual(2, result["skipped_transitions"])
        self.assertEqual(0, result["clock_lag_ms"])

    def test_explicit_reset_anchor_measures_lateness_and_rejects_bad_phase(self):
        rows = [{"advance": t, "t": t*1000, "rgb": (value,)*3, "gen": 1, "cmd": -1}
                for t, value in ((0, 255), (90, 0), (210, 255), (290, 0), (2010, 255))]
        for row in rows:
            row["output"] = row["t"] + 3000
        result = capture.phase_metrics(rows, phase_offset=0)
        self.assertEqual(13, result["output_lateness_max_ms"])
        self.assertEqual(13, result["output_lateness_mean_ms"])
        self.assertEqual(10, result["phase_lateness_max_ms"])
        self.assertEqual(10, result["phase_lateness_mean_ms"])
        self.assertFalse(capture.phase_metrics(rows, phase_offset=100)["available"])

    def test_incomplete_and_overflow_rejected(self):
        for text in ("", "LEDTRACE summary rows=1 dropped=0", "LEDTRACE summary rows=0 dropped=1"):
            with self.assertRaises(ValueError):
                capture.analyze(text)

    def test_existing_capture_is_not_replaced_or_stopped(self):
        commands = []
        def command(text, marker, timeout=20):
            commands.append(text)
            if text.endswith("status"):
                return "LEDTRACE status enabled=1 capacity=512 rows=12 dropped=0"
            return "Registered Plugins"
        result = capture.capture_session(command, lambda: None)
        self.assertFalse(result["completed"])
        self.assertTrue(result["foreign_trace_preserved"])
        self.assertNotIn("builtin_led_trace stop", commands)
        self.assertNotIn("builtin_led_trace start", commands)

    def test_uncertain_start_is_read_before_cleanup_without_replaying(self):
        commands = []; enabled = False
        def command(text, marker, timeout=20):
            nonlocal enabled
            commands.append(text)
            if text.endswith("status"):
                return f"LEDTRACE status enabled={int(enabled)} capacity=512 rows=0 dropped=0"
            if text.endswith("start"):
                enabled = True
                raise RuntimeError("lost acknowledgment")
            if text.endswith("stop"):
                enabled = False
                return "LEDTRACE stop done"
            return "Registered Plugins"
        result = capture.capture_session(command, lambda: None)
        self.assertFalse(result["completed"])
        self.assertTrue(result["trace_disabled"])
        self.assertEqual(1, commands.count("builtin_led_trace start"))
        self.assertEqual(1, commands.count("builtin_led_trace stop"))

    def test_failed_allocation_does_not_send_a_mutable_cleanup_command(self):
        commands = []
        def command(text, marker, timeout=20):
            commands.append(text)
            if text.endswith("status"):
                return "LEDTRACE status enabled=0 capacity=512 rows=0 dropped=0"
            if text.endswith("start"):
                raise RuntimeError("allocation failed")
            return "Registered Plugins"
        result = capture.capture_session(command, lambda: None)
        self.assertFalse(result["completed"])
        self.assertTrue(result["trace_disabled"])
        self.assertNotIn("builtin_led_trace stop", commands)

    def test_explicit_start_refusal_preserves_capture_started_by_another_owner(self):
        commands = []
        def command(text, marker, timeout=20):
            commands.append(text)
            if text.endswith("status"):
                return "LEDTRACE status enabled=0 capacity=512 rows=0 dropped=0"
            if text.endswith("start"):
                raise capture.TraceAlreadyActive("another owner started")
            return "Registered Plugins"
        result = capture.capture_session(command, lambda: None)
        self.assertFalse(result["completed"])
        self.assertTrue(result["foreign_trace_preserved"])
        self.assertNotIn("builtin_led_trace stop", commands)

if __name__ == "__main__":
    unittest.main()
