import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

import importlib.util
import unittest
from unittest.mock import patch

with patch.object(sys, 'path', [str(Path(__file__).resolve().parents[2] / 'diagnostic')] + sys.path):
    spec = importlib.util.spec_from_file_location('climate_fields', Path(__file__).resolve().parents[2] / 'Controllers/ClimateController/diagnostic/verify_climate_fields.py')
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)

class ClimateEvidenceTests(unittest.TestCase):
    def test_complete_snapshot_and_last_value(self):
        text = 'RACOOL_CLI v=1 type=climate_status mode=0 on=0 stale=0 debouncing=0 ir_tick=123 object_c=22.00 ambient_c=21.00 cpu_c=27.00 open_c=30.00 close_c=28.00 object_cal_c=0.10 ambient_cal_c=-0.20 enabled=1 manual=0 manual_state=0 strategy=0 object_source=0 ambient_source=0 delta_on=20 delta_off=10\n'
        self.assertEqual(0.1, helper.parse_settings(text)['object'])
        self.assertEqual(-0.2, helper.parse_settings(text)['ambient'])
        with self.assertRaises(RuntimeError): helper.parse_settings(text.replace('open_c=30.00', 'open_c=19.00'))
        with self.assertRaises(RuntimeError): helper.parse_settings(text.replace('manual_state=0', 'manual_state=1'))
        with self.assertRaises(RuntimeError): helper.parse_settings(text.replace('object_cal_c', 'missing'))
    def test_defaults_resolve_from_configuration_source(self):
        defaults = helper.expected_defaults()
        self.assertGreater(defaults['open'], defaults['close'])
        self.assertEqual({'open', 'close', 'manual', 'state', 'object', 'ambient','mode','object_source','ambient_source','delta_on','delta_off'}, set(defaults))
    def test_ambient_snapshot_preserves_sources_and_signed_delta(self):
        text = 'RACOOL_CLI v=1 type=climate_status mode=3 on=0 stale=0 debouncing=0 ir_tick=123 object_c=22.00 ambient_c=21.00 cpu_c=27.00 open_c=30.00 close_c=28.00 object_cal_c=0.10 ambient_cal_c=-0.20 enabled=1 manual=0 manual_state=0 strategy=1 object_source=2 ambient_source=3 delta_on=-10 delta_off=-20\n'
        result=helper.parse_settings(text)
        self.assertEqual(('AMBIENT_DELTA',2,3,-10,-20), tuple(result[k] for k in ('mode','object_source','ambient_source','delta_on','delta_off')))
        with self.assertRaises(RuntimeError):helper.parse_settings(text.replace('mode=3','mode=0'))
        with self.assertRaises(RuntimeError):helper.parse_settings(text.replace('mode=3','mode=2').replace('manual=0','manual=1'))

if __name__ == '__main__': unittest.main()
