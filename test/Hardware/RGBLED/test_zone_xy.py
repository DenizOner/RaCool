import unittest
from pathlib import Path
from diagnostic.verify_zone_xy import parse_xy,menu_states,blue_xy
class ZoneXYTest(unittest.TestCase):
    def test_parser_requires_matching_zone_and_valid_complete_pair(self):
        self.assertEqual((1234,4321),parse_xy('RACOOL_CLI v=1 type=zone_xy zone=cold x=1234 y=4321\n','cold'))
        for text in ('RACOOL_CLI v=1 type=zone_xy zone=warm x=1234 y=4321\n','RACOOL_CLI v=1 type=zone_xy zone=cold x=1 y=0\n','RACOOL_CLI v=1 type=zone_xy zone=cold x=65535 y=1\n','Zone XY cold: saved (0x0)'):
            self.assertIsNone(parse_xy(text,'cold'))
    def test_current_preset_states_and_blue_pair_are_discovered(self):
        root=Path(__file__).resolve().parents[3]
        states=menu_states(root)
        self.assertEqual({'cold','warm','hot','danger','freeze'},set(states))
        self.assertEqual(5,len(set(states.values())))
        self.assertEqual((9831,3933),blue_xy(root))
if __name__=='__main__':unittest.main()
