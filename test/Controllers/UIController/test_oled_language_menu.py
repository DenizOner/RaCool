import importlib.util
import unittest
from pathlib import Path

helper=Path(__file__).parent/'diagnostic/verify_oled_language_menu.py'
spec=importlib.util.spec_from_file_location('oled_language_menu_helper',helper)
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)


class OledLanguageMenuParserTests(unittest.TestCase):
    def test_state_is_discovered_after_comments_and_preserved_ids(self):
        header='typedef enum { UI_STATE_MONITOR=0, /* old */ UI_STATE_OLD=22, // preserved\n UI_STATE_SET_LANGUAGE, UI_STATE_COUNT } UIState;'
        self.assertEqual(23,module.language_state(header))

    def test_unknown_expressions_are_not_guessed(self):
        with self.assertRaisesRegex(ValueError,'Unsupported UI state expression'):
            module.language_state('typedef enum { UI_STATE_MONITOR=0, UI_STATE_OLD=(1<<2), UI_STATE_SET_LANGUAGE } UIState;')

    def test_absent_language_or_enum_is_rejected(self):
        for header in ['not an enum','typedef enum { UI_STATE_MONITOR=0, UI_STATE_COUNT } UIState;']:
            with self.subTest(header=header),self.assertRaises(ValueError):module.language_state(header)

    def test_menu_parser_rejects_partial_and_uses_latest_complete_state(self):
        self.assertIsNone(module.parse_menu('Current UI State : '))
        self.assertEqual(64,module.parse_menu('RACOOL_CLI v=1 type=menu_status state=0 registry_label_hex=4d6f6e69746f72\r\nRACOOL_CLI v=1 type=menu_status state=64 registry_label_hex=4c616e6775616765\r\r\n'))


if __name__=='__main__':unittest.main()
