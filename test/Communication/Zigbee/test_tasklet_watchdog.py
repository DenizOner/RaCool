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
LOG_816_SYNTH = """task_wdt:  - IDLE (CPU 0)
task_wdt: CPU 0: Zigbee_main
Core  0 register dump:
MEPC : 0x1010 RA : 0x101c
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

LOG_REAL_816 = """task_wdt:  - IDLE (CPU 0)
task_wdt: CPU 0: Zigbee_main
Core  0 register dump:
MEPC    : 0x4210c816  RA      : 0x4210c828  SP      : 0x40820730  GP      : 0x40815824
TP      : 0x40820930  T0      : 0x40022494  T1      : 0x9a400000  T2      : 0x32800000
S0/FP   : 0xfffffffd  S1      : 0x4083665c  A0      : 0x4083665c  A1      : 0x00000000
A2      : 0x00000000  A3      : 0x00000005  A4      : 0x40836660  A5      : 0x00000000
"""
# Sized nm symbols from the SHA-256-matched original firmware ELF (Issue #2).
SYMBOLS_REAL = """4210c7e0 0000004c T tasklet_sched_process
40836630 0000030c b s_nwk_fwd
"""
DISASM_REAL = """    4210c806: 4104 lw s1,0(a0)
    4210c808: 00450713 addi a4,a0,4
    4210c80c: 0330000f fence rw,rw
    4210c810: 431c lw a5,0(a4)
    4210c812: 0230000f fence r,rw
    4210c816: 8b85 andi a5,a5,1
    4210c818: eb81 bnez a5,4210c828
    4210c81a: 668727af amoand.w.aqrl a5,s0,(a4)
    4210c81e: 8b89 andi a5,a5,2
"""


class TaskletWatchdogTests(unittest.TestCase):
    def test_recognizes_matching_snapshot_without_claiming_cause(self):
        result = module.analyze(LOG, SYMBOLS, DISASM)
        self.assertEqual(result["mepc"], "0x1018")
        self.assertEqual(result["ra"], "0x101c")
        self.assertEqual(result["current"], "0x202c")
        self.assertEqual(result["next"], "0x202c")
        self.assertTrue(result["self_link_consistent"])
        self.assertTrue(result["current_in_nwk_fwd"])
        self.assertEqual(result["nwk_fwd_offset"], "0x2c")
        self.assertIn("do not identify", result["limitation"])

    def test_recognizes_mepc_at_first_andi_without_claiming_cause(self):
        result = module.analyze(LOG_816_SYNTH, SYMBOLS, DISASM)
        self.assertEqual(result["mepc"], "0x1010")
        self.assertEqual(result["ra"], "0x101c")
        self.assertEqual(result["current"], "0x202c")
        self.assertEqual(result["next"], "0x202c")
        self.assertTrue(result["self_link_consistent"])
        self.assertTrue(result["current_in_nwk_fwd"])
        self.assertEqual(result["nwk_fwd_offset"], "0x2c")
        self.assertIn("do not identify", result["limitation"])

    def test_recognizes_real_esp32c6_watchdog_snapshot_at_816_and_81e(self):
        result_816 = module.analyze(LOG_REAL_816, SYMBOLS_REAL, DISASM_REAL)
        self.assertEqual(result_816["mepc"], "0x4210c816")
        self.assertEqual(result_816["ra"], "0x4210c828")
        self.assertEqual(result_816["current"], "0x4083665c")
        self.assertEqual(result_816["next"], "0x4083665c")
        self.assertTrue(result_816["self_link_consistent"])
        self.assertTrue(result_816["current_in_nwk_fwd"])
        self.assertEqual(result_816["nwk_fwd_offset"], "0x2c")
        self.assertIn("do not identify", result_816["limitation"])

        log_81e = LOG_REAL_816.replace("0x4210c816", "0x4210c81e")
        result_81e = module.analyze(log_81e, SYMBOLS_REAL, DISASM_REAL)
        self.assertEqual(result_81e["mepc"], "0x4210c81e")
        self.assertEqual(result_81e["ra"], "0x4210c828")
        self.assertTrue(result_81e["self_link_consistent"])
        self.assertEqual(result_81e["nwk_fwd_offset"], "0x2c")

    def test_distinct_next_is_not_self_link(self):
        for log in (LOG.replace("S1 : 0x202c", "S1 : 0x2030"),
                    LOG_816_SYNTH.replace("S1 : 0x202c", "S1 : 0x2030"),
                    LOG_REAL_816.replace("S1      : 0x4083665c", "S1      : 0x40836660")):
            with self.subTest(log=log):
                result = module.analyze(log, SYMBOLS_REAL if "4210c816" in log else SYMBOLS,
                                        DISASM_REAL if "4210c816" in log else DISASM)
                self.assertFalse(result["self_link_consistent"])

    def test_does_not_infer_cause_from_a5_register_value(self):
        for a5_val in ("0x00000000", "0x00000001", "0x00000002", "0xffffffff"):
            with self.subTest(a5=a5_val):
                log = LOG_REAL_816.replace("A5      : 0x00000000", f"A5      : {a5_val}")
                result = module.analyze(log, SYMBOLS_REAL, DISASM_REAL)
                self.assertTrue(result["self_link_consistent"])
                self.assertNotIn("cause", result)
                self.assertNotIn("a5", result)
                self.assertIn("do not identify", result["limitation"])

    def test_rejects_unsupported_mepc_positions_in_scheduler_sequence(self):
        for unsupported_mepc in ("0x1000", "0x1002", "0x1006", "0x100a", "0x100c", "0x1012", "0x1014"):
            with self.subTest(mepc=unsupported_mepc):
                log = LOG.replace("0x1018", unsupported_mepc)
                with self.assertRaises(ValueError) as ctx:
                    module.analyze(log, SYMBOLS, DISASM)
                self.assertIn("Unrecognized scheduler instruction sequence", str(ctx.exception))

    def test_rejects_missing_instructions_in_both_layouts(self):
        lines = [line for line in DISASM.splitlines() if line.strip()]
        for idx in range(len(lines)):
            reduced_lines = lines[:idx] + lines[idx + 1:]
            reduced_asm = "\n".join(reduced_lines) + "\n"
            for log in (LOG, LOG_816_SYNTH):
                with self.subTest(removed=lines[idx].strip(), mepc=log.split("MEPC : ")[1].split()[0]):
                    with self.assertRaises(ValueError) as ctx:
                        module.analyze(log, SYMBOLS, reduced_asm)
                    self.assertIn("Unrecognized scheduler instruction sequence", str(ctx.exception))

    def test_rejects_altered_instructions_in_both_layouts(self):
        replacements = [
            ("lw s1,0(a0)", "lw s2,0(a0)"),
            ("addi a4,a0,4", "addi a4,a0,8"),
            ("fence rw,rw", "fence r,rw"),
            ("lw a5,0(a4)", "lw a4,0(a4)"),
            ("fence r,rw", "fence rw,w"),
            ("andi a5,a5,1", "andi a5,a5,3"),
            ("bnez a5,1022", "beqz a5,1022"),
            ("bnez a5,1022", "bnez a4,1022"),
            ("amoand.w.aqrl a5,s0,(a4)", "amoor.w.aqrl a5,s0,(a4)"),
            ("andi a5,a5,2", "andi a5,a5,0"),
        ]
        for target, replacement in replacements:
            altered_asm = DISASM.replace(target, replacement)
            for log in (LOG, LOG_816_SYNTH):
                with self.subTest(target=target, replacement=replacement, mepc=log.split("MEPC : ")[1].split()[0]):
                    with self.assertRaises(ValueError) as ctx:
                        module.analyze(log, SYMBOLS, altered_asm)
                    self.assertIn("Unrecognized scheduler instruction sequence", str(ctx.exception))

    def test_accepts_disassembly_branch_symbol_annotation(self):
        annotated = DISASM_REAL.replace(
            "bnez a5,4210c828",
            "bnez a5,4210c828 <tasklet_sched_process+0x48>")
        for log in (LOG_REAL_816,
                    LOG_REAL_816.replace("0x4210c816", "0x4210c81e")):
            with self.subTest(mepc=log.split("MEPC")[1].split()[1]):
                result = module.analyze(log, SYMBOLS_REAL, annotated)
                self.assertEqual(result["nwk_fwd_offset"], "0x2c")

    def test_rejects_tampered_branch_target_in_both_layouts(self):
        fixtures = [
            (LOG, SYMBOLS, DISASM, "bnez a5,1022"),
            (LOG_816_SYNTH, SYMBOLS, DISASM, "bnez a5,1022"),
            (LOG_REAL_816, SYMBOLS_REAL, DISASM_REAL, "bnez a5,4210c828"),
            (LOG_REAL_816.replace("0x4210c816", "0x4210c81e"),
             SYMBOLS_REAL, DISASM_REAL, "bnez a5,4210c828"),
        ]
        for log, symbols, disasm, original_branch in fixtures:
            for wrong_target in ("0", "1000", "4210c822", "nothex"):
                with self.subTest(mepc=log.split("MEPC")[1].split()[1],
                                  wrong_target=wrong_target):
                    tampered = disasm.replace(
                        original_branch, "bnez a5," + wrong_target)
                    with self.assertRaisesRegex(
                            ValueError, "Unrecognized scheduler instruction sequence"):
                        module.analyze(log, symbols, tampered)

    def test_rejects_bad_branch_annotation_and_wrong_annotated_target(self):
        for replacement in ("bnez a5,4210c822 <tasklet_sched_process+0x48>",
                            "bnez a5,4210c828 <corrupt",
                            "bnez a5,4210c828 junk"):
            with self.subTest(replacement=replacement):
                tampered = DISASM_REAL.replace("bnez a5,4210c828", replacement)
                with self.assertRaisesRegex(
                        ValueError, "Unrecognized scheduler instruction sequence"):
                    module.analyze(LOG_REAL_816, SYMBOLS_REAL, tampered)

    def test_rejects_wrong_dump_or_changed_instructions(self):
        for log, asm in [(LOG.replace("Zigbee_main", "other"), DISASM),
                         (LOG.replace("task_wdt:  - IDLE (CPU 0)", "other"), DISASM),
                         (LOG + LOG, DISASM),
                         (LOG, DISASM.replace("lw s1,0(a0)", "lw s2,0(a0)")),
                         (LOG, DISASM.replace("addi a4,a0,4", "mv a0,s1"))]:
            with self.subTest(log=log, asm=asm), self.assertRaises(ValueError):
                module.analyze(log, SYMBOLS, asm)

    def test_rejects_wrong_or_ambiguous_elf_symbols(self):
        for symbols in (SYMBOLS.replace("00001000", "00003000"),
                        SYMBOLS + "00003000 00000040 T tasklet_sched_process\n",
                        SYMBOLS.replace("s_nwk_fwd", "other"),
                        SYMBOLS.replace("tasklet_sched_process", "other"),
                        SYMBOLS.replace("00000040", "nothex")):
            with self.subTest(symbols=symbols), self.assertRaises(ValueError):
                module.analyze(LOG, symbols, DISASM)

    def test_rejects_missing_register_and_reports_outside_object(self):
        for reg in ("MEPC : 0x1018", "RA : 0x101c", "S1 : 0x202c", "A0 : 0x202c"):
            with self.subTest(missing_reg=reg), self.assertRaises(ValueError):
                module.analyze(LOG.replace(reg, ""), SYMBOLS, DISASM)
        result = module.analyze(LOG.replace("0x202c", "0x2500"), SYMBOLS, DISASM)
        self.assertTrue(result["self_link_consistent"])
        self.assertFalse(result["current_in_nwk_fwd"])
        self.assertIsNone(result["nwk_fwd_offset"])

    def test_rejects_snapshot_outside_scheduler_bounds(self):
        # MEPC outside tasklet_sched_process [0x1000, 0x1040)
        with self.assertRaises(ValueError) as ctx:
            module.analyze(LOG.replace("MEPC : 0x1018", "MEPC : 0x1050"), SYMBOLS, DISASM)
        self.assertIn("Snapshot does not resolve inside tasklet scheduler", str(ctx.exception))

        # RA outside tasklet_sched_process [0x1000, 0x1040)
        with self.assertRaises(ValueError) as ctx:
            module.analyze(LOG.replace("RA : 0x101c", "RA : 0x2000"), SYMBOLS, DISASM)
        self.assertIn("Snapshot does not resolve inside tasklet scheduler", str(ctx.exception))


if __name__ == "__main__":
    unittest.main()
