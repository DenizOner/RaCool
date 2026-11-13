import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "diagnostic"))
from fan_sources_evidence import parse_sources, config_values, require_empty_slots
from verify_fan_sources import verify_owned_restore_state, background_errors, check_before_command
import verify_fan_sources

SAMPLE = """FAN_CONFIG mode=3 object_on=30.00 object_off=28.00 delta_on=-1.0 delta_off=-2.0
SOURCE object configured=2 effective=0 status=1 valid=1 temperature=19.00 age_ms=100
SOURCE ambient configured=1 effective=1 status=0 valid=1 temperature=20.00 age_ms=500
DIFFERENCE valid=1 temperature=-1.00
EXT1 state=1 value=20.00 last_update_ms=4294967294 age_ms=500
EXT2 state=2 value=23.00 last_update_ms=10 age_ms=900000
EXT3 state=0 value=0.00 last_update_ms=0 age_ms=0
"""


class FanSourcesEvidenceTests(unittest.TestCase):
    def test_inter_command_errors_and_reboot_are_not_discarded(self):
        self.assertEqual(['E (123) Zigbee: failed'], background_errors('I (1) ready\nE (123) Zigbee: failed\n'))
        self.assertEqual(['E (15439) Zigbee: failed'], background_errors('racool> CLE (15439) Zigbee: failed\r\n'))
        self.assertEqual([], background_errors('I (1) ready\n'))
        for text in ('Guru Meditation Error', 'ESP-ROM:esp32c6', 'assert failed'):
            with self.subTest(text=text), self.assertRaises(RuntimeError):
                background_errors(text)

    def test_pending_error_blocks_normal_mutation_but_allows_restoration(self):
        evidence={}
        with self.assertRaisesRegex(RuntimeError, 'before command'):
            check_before_command('E (4) Zigbee: failed\n', False, evidence, 'CLIMATE MODE ON')
        self.assertEqual(['E (4) Zigbee: failed'], evidence['asynchronous_errors'])
        restored={'completed':True}
        check_before_command('racool> X\x1b[31mE (4) Zigbee: failed\x1b[0m\n', True, restored, 'CLIMATE MODE OFF')
        self.assertEqual(['E (4) Zigbee: failed'], restored['asynchronous_errors'])
        self.assertFalse(restored['completed'])
        with self.assertRaises(RuntimeError):
            check_before_command('ESP-ROM:esp32c6', True, {}, 'CLIMATE MODE OFF')

    def test_restart_waits_for_new_ready_boot_and_records_interleaved_errors(self):
        chunks=iter(['RACOOL_CLI v=1 type=power_restart result=queued delay_ms=500 error=0\n',
                     'ESP-ROM:esp32c6\nSW_CPU\nX\x1b[31mE (5) Zigbee: failed\x1b[0m\n',
                     'SYSTEM INITIALIZATION COMPLETE\napp_main: System ready.\nracool>\n'])
        ticks=iter(i*.5 for i in range(100))
        result=verify_fan_sources.collect_restart(lambda deadline: next(chunks, ''), clock=lambda:next(ticks))
        self.assertTrue(result['acknowledged'])
        self.assertEqual(['E (5) Zigbee: failed'], result['errors'])

    def test_restart_does_not_infer_boot_from_ack_or_ignore_panic(self):
        for chunks in (["RACOOL_CLI v=1 type=power_restart result=queued delay_ms=500 error=0\n"],
                       ["ESP-ROM:esp32c6\nGuru Meditation\n"],
                       ["ESP-ROM:esp32c6\nassert failed\n"],
                       ["ESP-ROM:esp32c6\nESP-ROM:esp32c6\n"]):
            values=iter(chunks);ticks=iter(i for i in range(100))
            with self.subTest(chunks=chunks), self.assertRaises(RuntimeError):
                verify_fan_sources.collect_restart(lambda deadline:next(values,''), timeout=4, clock=lambda:next(ticks))

    def test_restart_boot_without_ack_remains_ambiguous(self):
        chunks=iter(['ESP-ROM:esp32c6\nSW_CPU\nSYSTEM INITIALIZATION COMPLETE\napp_main: System ready.\nracool>\n'])
        ticks=iter(i*.5 for i in range(100))
        result=verify_fan_sources.collect_restart(lambda deadline:next(chunks,''), clock=lambda:next(ticks))
        self.assertFalse(result['acknowledged'])
        self.assertEqual([],result['errors'])

    def test_uncertain_restart_never_attempts_developer_mode_write(self):
        class Session:
            calls=0
            def close(self): self.calls+=1
        session=Session();evidence={'completed':True};saved=[]
        verify_fan_sources.close_developer_session(session,True,evidence,lambda:saved.append(True))
        self.assertEqual(0,session.calls)
        self.assertFalse(evidence['completed'])
        self.assertIn('uncertain',evidence['developer_mode_restore_error'])
        self.assertEqual([True],saved)
        verify_fan_sources.close_developer_session(session,False,evidence,lambda:saved.append(True))
        self.assertEqual(1,session.calls)

    def test_failed_restoration_observation_returns_failure_exit_code(self):
        self.assertEqual(1,verify_fan_sources.result_exit_code({'completed':False,'restored':{'new_boot':True}}))
        self.assertEqual(1,verify_fan_sources.result_exit_code({}))
        self.assertEqual(0,verify_fan_sources.result_exit_code({'completed':True}))

    def test_cleanup_requires_values_from_original_or_submitted_commands(self):
        current=parse_sources(SAMPLE)
        original=config_values(current)
        original.update(mode=0,object_source=0,ambient_source=0,delta_on=2.0,delta_off=1.0)
        submitted=[{'text':'CLIMATE MODE AMBIENT_DELTA --machine'},
                   {'text':'CLIMATE SOURCE OBJECT 2 --machine'},
                   {'text':'CLIMATE SOURCE AMBIENT 1 --machine'},
                   {'text':'CLIMATE DELTA -1 -2 --machine'},
                   {'text':'CLIMATE EXTERNAL 1 20 --machine'},
                   {'text':'CLIMATE EXTERNAL 2 23 --machine'}]
        self.assertEqual(config_values(current),verify_owned_restore_state(current,original,submitted))
        with self.assertRaisesRegex(RuntimeError,'foreign fan'):
            verify_owned_restore_state(current,dict(original,object_on=31),submitted)
        with self.assertRaisesRegex(RuntimeError,'foreign measurement'):
            verify_owned_restore_state(current,original,submitted[:-1])
    def test_signed_difference_and_full_timestamp(self):
        actual = parse_sources(SAMPLE.replace("\n", "\r\r\n"))
        self.assertEqual(-1, actual["difference"])
        self.assertEqual(4294967294, actual["slots"][1]["last_update_ms"])
        self.assertEqual(2, config_values(actual)["object_source"])

    def test_latest_incomplete_response_rejected(self):
        with self.assertRaises(ValueError):
            parse_sources(SAMPLE + "FAN_CONFIG mode=3\n")

    def test_wrong_difference_rejected(self):
        with self.assertRaises(ValueError):
            parse_sources(SAMPLE.replace("temperature=-1.00", "temperature=1.00"))

    def test_nonfinite_configuration_rejected(self):
        with self.assertRaises(ValueError):
            parse_sources(SAMPLE.replace("delta_on=-1.0", "delta_on=nan"))

    def test_invalid_inputs_cannot_produce_valid_difference(self):
        with self.assertRaises(ValueError):
            parse_sources(SAMPLE.replace("status=1 valid=1", "status=2 valid=0"))

    def test_foreign_slots_rejected(self):
        with self.assertRaises(ValueError):
            require_empty_slots(parse_sources(SAMPLE))

    def test_empty_slots_accepted(self):
        text = SAMPLE.replace("state=1 value=20.00 last_update_ms=4294967294 age_ms=500", "state=0 value=0.00 last_update_ms=0 age_ms=0")
        text = text.replace("state=2 value=23.00 last_update_ms=10 age_ms=900000", "state=0 value=0.00 last_update_ms=0 age_ms=0")
        require_empty_slots(parse_sources(text))

    def test_false_difference_cannot_hide_valid_inputs(self):
        with self.assertRaises(ValueError):
            parse_sources(SAMPLE.replace("DIFFERENCE valid=1 temperature=-1.00", "DIFFERENCE valid=0 temperature=nan"))

    def test_source_health_must_match_configured_and_effective(self):
        with self.assertRaises(ValueError):
            parse_sources(SAMPLE.replace("effective=0 status=1", "effective=0 status=0"))

    def test_invalid_nonfinite_measurement_serializes_as_absent(self):
        text=SAMPLE.replace("status=1 valid=1 temperature=19.00", "status=2 valid=0 temperature=nan")
        text=text.replace("DIFFERENCE valid=1 temperature=-1.00", "DIFFERENCE valid=0 temperature=nan")
        self.assertIsNone(parse_sources(text)["sources"]["object"]["temperature"])


if __name__ == "__main__":
    unittest.main()
