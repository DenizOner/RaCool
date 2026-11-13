import importlib.util
import unittest
from pathlib import Path

path = Path(__file__).parent / "diagnostic/verify_language.py"
spec = importlib.util.spec_from_file_location("language_live", path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class LanguageEvidenceTests(unittest.TestCase):
    def test_current_locale_requires_a_status_not_write_acknowledgment(self):
        self.assertEqual("tr", module.parse_locale("RACOOL_CLI v=1 type=language locale=tr\r\n"))
        self.assertEqual("en", module.parse_locale("RACOOL_CLI v=1 type=language locale=en\n"))
        self.assertIsNone(module.parse_locale("Language applied: English\n"))
        self.assertIsNone(module.parse_locale("Dil değiştirilmedi; kayıt başarısız: 1"))

    def test_last_complete_screen_readback_is_used(self):
        self.assertEqual("SUSPENDED", module.parse_screen("RACOOL_CLI v=1 type=ui screen=active service=2 boot=idle\nRACOOL_CLI v=1 type=ui screen=suspended service=2 boot=idle\n"))
        self.assertIsNone(module.parse_screen("UI wake event queued"))

    def test_machine_records_reject_partial_unknown_versions_and_human_output(self):
        for text in ("RACOOL_CLI v=1 type=language locale=tr", "RACOOL_CLI v=2 type=language locale=tr\n",
                     "LANGUAGE --machine\r\n", "Dil: Türkçe\n", "Language: English\n"):
            self.assertIsNone(module.parse_locale(text))
        self.assertEqual("uk", module.parse_locale("RACOOL_CLI v=1 type=language locale=uk\n"))
        self.assertEqual("pt-BR", module.parse_locale("RACOOL_CLI v=1 type=language locale=pt-BR\r\r\n"))
        self.assertIsNone(module.parse_locale("RACOOL_CLI v=1 type=language locale=NOT_VALID\n"))
        self.assertIsNone(module.parse_screen("RACOOL_CLI v=1 type=ui screen=active service=2 boot=idle"))
        self.assertIsNone(module.parse_screen("Screen State: SUSPENDED\n"))

    def test_saved_fallback_and_unavailable_configuration_are_distinct(self):
        self.assertEqual({"saved": "de", "active": "tr", "restorable": False}, module.parse_config("RACOOL_CLI v=1 type=language_config saved=de active=tr restorable=0\n"))
        self.assertIsNone(module.parse_config("RACOOL_CLI v=1 type=language_config result=unavailable\n"))
        self.assertIsNone(module.parse_config("RACOOL_CLI v=1 type=language_config saved=tr active=tr restorable=1"))

    def test_machine_set_requires_a_complete_valid_result(self):
        self.assertEqual("en", module.parse_set("RACOOL_CLI v=1 type=language_set result=applied locale=en\n")["locale"])
        self.assertEqual("invalid_locale", module.parse_set("RACOOL_CLI v=1 type=language_set result=invalid_locale\n")["result"])
        self.assertEqual(-1, module.parse_set("RACOOL_CLI v=1 type=language_set result=save_failed error=-1\n")["error"])
        for text in ("RACOOL_CLI v=1 type=language_set result=applied\n", "RACOOL_CLI v=1 type=language_set result=save_failed\n", "Language applied: English\n"):
            self.assertIsNone(module.parse_set(text))

    def test_catalog_count_and_uniqueness_are_required(self):
        entries="RACOOL_CLI v=1 type=language_catalog locale=tr\nRACOOL_CLI v=1 type=language_catalog locale=en\n"
        self.assertEqual(["tr", "en"], module.parse_catalogs(entries+"RACOOL_CLI v=1 type=language_catalog_end count=2\n"))
        self.assertIsNone(module.parse_catalogs(entries))
        self.assertIsNone(module.parse_catalogs(entries+"RACOOL_CLI v=1 type=language_catalog_end count=3\n"))
        self.assertIsNone(module.parse_catalogs(entries.replace("locale=en", "locale=tr")+"RACOOL_CLI v=1 type=language_catalog_end count=2\n"))


if __name__ == "__main__": unittest.main()
