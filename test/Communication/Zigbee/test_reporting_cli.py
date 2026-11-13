import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import unittest
from Communication.Zigbee.diagnostic.verify_reporting_cli import has_fresh_console, validate_mqtt_readback, PROPERTIES

class ReportingBootEvidence(unittest.TestCase):
    def test_machine_intervals_validate_bounds_enabled_and_duplicates(self):
        from Communication.Zigbee.diagnostic.reporting_cli_evidence import report_values
        valid="RACOOL_CLI v=1 type=report_interval report=fan seconds=0 enabled=0"
        self.assertEqual({'fan':0},report_values(valid))
        self.assertEqual({},report_values('fan: 0 s'))
        for text in [valid+"\n"+valid,valid.replace('seconds=0','seconds=3601'),valid.replace('enabled=0','enabled=1'),valid.replace('seconds=0','seconds=bad')]:
            with self.assertRaises(ValueError):report_values(text)

    def test_old_prompt_cannot_prove_new_console_ready(self):
        self.assertFalse(has_fresh_console("racool> restarting... ESP-ROM:boot"))
        self.assertFalse(has_fresh_console("racool>"))
        self.assertTrue(has_fresh_console("racool> restarting... ESP-ROM:boot app ready racool>"))

    def test_latest_reset_requires_its_own_prompt(self):
        self.assertFalse(has_fresh_console("ESP-ROM:first racool> ESP-ROM:second"))
        self.assertTrue(has_fresh_console("ESP-ROM:first racool> ESP-ROM:second racool>"))

    def test_cli_readback_requires_every_fresh_matching_endpoint_property(self):
        reads = [{"property": property, "value": 0,
                  "evidence": {"kind": "mqtt_state_after_get", "actual": 0}}
                 for property in PROPERTIES.values()]
        document = {"completed": True, "initialReads": reads}
        expected = dict.fromkeys(PROPERTIES, 0)
        self.assertEqual(validate_mqtt_readback(document, expected), reads)
        for bad in ({**document, "completed": False}, {**document, "errors": ["failure"]},
                    {**document, "initialReads": reads[:-1]}):
            with self.assertRaises(RuntimeError):
                validate_mqtt_readback(bad, expected)
        reads[0]["evidence"]["kind"] = "retained"
        with self.assertRaises(RuntimeError):
            validate_mqtt_readback(document, expected)
        reads[0]["evidence"]["kind"] = "mqtt_state_after_get"
        reads[0]["value"] = 1
        with self.assertRaises(RuntimeError):
            validate_mqtt_readback(document, expected)

if __name__ == "__main__":
    unittest.main()
