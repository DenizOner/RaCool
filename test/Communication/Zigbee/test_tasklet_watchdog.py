import importlib.util
import unittest
from pathlib import Path

path = Path(__file__).parent / "diagnostic" / "analyze_tasklet_watchdog.py"
spec = importlib.util.spec_from_file_location("tasklet_watchdog", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

LOG = """task_wdt:  - IDLE (CPU 0)
task_wdt: CPU 0: Zigbee_main
Core  0 register dump:
MEPC : 0x1018 RA : 0x101c
S1 : 0x202c A0 : 0x202c
"""
SYMBOLS = """00001000 00000040 T tasklet_sched_process
00002000 00000300 b s_nwk_fwd
"""
DISASM = """    1000: 4104 lw s1,0(a0)
    1002: 00450713 addi a4,a0,4
    1006: 0330000f fence rw,rw
    100a: 431c lw a5,0(a4)
    100c: 0230000f fence r,rw
    1010: 8b85 andi a5,a5,1
    1012: eb81 bnez a5,1022
    1014: 668727af amoand.w.aqrl a5,s0,(a4)
    1018: 8b89 andi a5,a5,2
"""


class TaskletWatchdogTests(unittest.TestCase):
    def test_recognizes_matching_snapshot_without_claiming_cause(self):
        result = module.analyze(LOG, SYMBOLS, DISASM)
        self.assertTrue(result["self_link_consistent"])
        self.assertEqual(result["nwk_fwd_offset"], "0x2c")
        self.assertIn("do not identify", result["limitation"])

    def test_distinct_next_is_not_self_link(self):
        result = module.analyze(LOG.replace("S1 : 0x202c", "S1 : 0x2030"), SYMBOLS, DISASM)
        self.assertFalse(result["self_link_consistent"])

    def test_rejects_wrong_dump_or_changed_instructions(self):
        for log, asm in [(LOG.replace("Zigbee_main", "other"), DISASM),
                         (LOG + LOG, DISASM),
                         (LOG, DISASM.replace("lw s1,0(a0)", "lw s2,0(a0)")),
                         (LOG, DISASM.replace("addi a4,a0,4", "mv a0,s1"))]:
            with self.subTest(log=log, asm=asm), self.assertRaises(ValueError):
                module.analyze(log, SYMBOLS, asm)

    def test_rejects_wrong_or_ambiguous_elf_symbols(self):
        for symbols in (SYMBOLS.replace("00001000", "00003000"),
                        SYMBOLS + "00003000 00000040 T tasklet_sched_process\n",
                        SYMBOLS.replace("s_nwk_fwd", "other")):
            with self.subTest(symbols=symbols), self.assertRaises(ValueError):
                module.analyze(LOG, symbols, DISASM)

    def test_rejects_missing_register_and_reports_outside_object(self):
        with self.assertRaises(ValueError):
            module.analyze(LOG.replace("RA : 0x101c", ""), SYMBOLS, DISASM)
        result = module.analyze(LOG.replace("0x202c", "0x2500"), SYMBOLS, DISASM)
        self.assertTrue(result["self_link_consistent"])
        self.assertFalse(result["current_in_nwk_fwd"])
        self.assertIsNone(result["nwk_fwd_offset"])


if __name__ == "__main__":
    unittest.main()
