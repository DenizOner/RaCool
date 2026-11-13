import importlib.util
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("catalog_generator", ROOT / "tools/generate_translations.py")
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class CatalogGenerationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        for locale in ("en", "tr"):
            self.write(locale, {"cli.result": "Value: %s %lu\n"})

    def write(self, locale, messages):
        (self.directory / f"{locale}.json").write_text(json.dumps({"locale": locale,
            "name": locale, "messages": messages}, ensure_ascii=False), encoding="utf-8")

    def test_project_catalogs_are_valid(self):
        self.assertEqual(2, len(generator.load_catalogs(ROOT / "resources/translations")))

    def test_oled_byte_limit_prevents_partial_utf8_in_widget_buffers(self):
        for locale in ("en", "tr"):
            self.write(locale, {"menu.sample": "x" * 96})
        self.assertEqual(2, len(generator.load_catalogs(self.directory)))
        self.write("tr", {"menu.sample": "Ё" * 49})
        with self.assertRaisesRegex(ValueError, "OLED message exceeds UTF-8 buffer limit"):
            generator.load_catalogs(self.directory)
        self.write("tr", {"menu.sample": "и\u0306" * 48})
        catalogs = generator.load_catalogs(self.directory)
        self.assertEqual("й" * 48, next(c for c in catalogs if c["locale"] == "tr")["messages"]["menu.sample"])

    def test_decomposed_latin_and_cyrillic_are_normalized_before_rendering(self):
        text = "Tu\u0308rkc\u0327e и\u0306 е\u0308: %s %lu\n"
        self.write("tr", {"cli.result": text})
        catalog = next(c for c in generator.load_catalogs(self.directory) if c["locale"] == "tr")
        self.assertEqual("Türkçe й ё: %s %lu\n", catalog["messages"]["cli.result"])

    def test_new_language_is_discovered_without_manifest_registration(self):
        self.write("bg", {"cli.result": "Стойност: %s %lu\n"})
        count, keys = generator.generate(self.directory, self.directory / "output")
        self.assertEqual((3, 1), (count, keys))
        generated = (self.directory / "output/LocalizationCatalog.generated.c").read_text()
        self.assertIn('"bg"', generated)
        self.assertNotIn("Стойност", generated)  # UTF-8 bytes use portable C octal escapes.

    def test_duplicate_keys_are_rejected(self):
        path = self.directory / "tr.json"
        path.write_text('{"locale":"tr","name":"tr","messages":{"cli.result":"a","cli.result":"b"}}')
        with self.assertRaisesRegex(ValueError, "Duplicate JSON key"):
            generator.load_catalogs(self.directory)

    def test_missing_and_extra_keys_are_rejected(self):
        for messages in ({"cli.other": "x"}, {"cli.result": "%s %lu", "cli.other": "x"}):
            with self.subTest(messages=messages):
                self.write("tr", messages)
                with self.assertRaisesRegex(ValueError, "Message keys differ"):
                    generator.load_catalogs(self.directory)

    def test_printf_abi_mismatch_is_rejected(self):
        for text in ("%s %u", "%lu %s", "%s", "%s %ld"):
            with self.subTest(text=text):
                self.write("tr", {"cli.result": text})
                with self.assertRaisesRegex(ValueError, "Format contract differs"):
                    generator.load_catalogs(self.directory)

    def test_unsafe_or_unsupported_formats_are_rejected(self):
        for text in ("%n", "%*s", "%1$s", "%", "%5%"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                generator.format_signature(text)
        self.assertEqual(["s", "lu"], generator.format_signature("%% %20s %08lu"))

    def test_locale_filename_must_match(self):
        path = self.directory / "tr.json"
        catalog = json.loads(path.read_text())
        catalog["locale"] = "bg"
        path.write_text(json.dumps(catalog))
        with self.assertRaisesRegex(ValueError, "match its filename"):
            generator.load_catalogs(self.directory)

    def test_invalid_utf8_nul_and_oversize_text_are_rejected(self):
        for text in ("\0", "x" * 2049):
            self.write("tr", {"cli.result": text})
            with self.assertRaisesRegex(ValueError, "Invalid message text"):
                generator.load_catalogs(self.directory)
        (self.directory / "tr.json").write_bytes(b"\xff")
        with self.assertRaises(UnicodeDecodeError):
            generator.load_catalogs(self.directory)

    def test_identifier_collision_is_rejected(self):
        self.write("tr", {"cli.a_b": "x", "cli_a.b": "x"})
        with self.assertRaisesRegex(ValueError, "identifier collision"):
            generator.load_catalogs(self.directory)

    def test_unchanged_generation_preserves_files(self):
        output = self.directory / "output"
        generator.generate(self.directory, output)
        before = {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in output.iterdir()}
        generator.generate(self.directory, output)
        after = {path.name: (path.read_bytes(), path.stat().st_mtime_ns) for path in output.iterdir()}
        self.assertEqual(before, after)

    def test_english_and_translation_cannot_change_the_c_argument_contract_together(self):
        sources = self.directory / "src"
        sources.mkdir()
        source = sources / "consumer.c"
        source.write_text('LOC_PRINTF(catalog, LOC_CLI_RESULT, "Value: %s %lu\\n", name, count);')
        catalogs = generator.load_catalogs(self.directory)
        generator.validate_source_contracts(catalogs, sources)
        for locale in ("tr", "en"):
            self.write(locale, {"cli.result": "Value: %s %u\n"})
        with self.assertRaisesRegex(ValueError, "Source format contract differs"):
            generator.generate(self.directory, self.directory / "output", sources)

    def test_comments_and_quoted_examples_do_not_create_source_contracts(self):
        sources = self.directory / "src"
        sources.mkdir()
        (sources / "consumer.c").write_text('''
// LOC_PRINTF(catalog, LOC_ABSENT, "%n", ptr);
/* LOC_PRINTF(catalog, LOC_ABSENT, "%n", ptr); */
const char *example = "LOC_PRINTF(catalog, LOC_ABSENT, usage)";
LOC_PRINTF(catalog, LOC_CLI_RESULT, "Value: %s " "%lu\\n", name, count);
''')
        generator.validate_source_contracts(generator.load_catalogs(self.directory), sources)

    def test_real_c_catalog_selection_preserves_response_snapshot(self):
        output = self.directory / "compiled"
        generator.generate(ROOT / "resources/translations", output)
        harness = output / "selection.c"
        harness.write_text(r'''
#include "Core/Localization/Localization.h"
#include <assert.h>
#include <string.h>
int main(void) {
    const LocalizationCatalog *first = Localization_Snapshot();
    assert(first && strcmp(first->code, "tr") == 0);
    assert(Localization_Count() == 2);
    assert(Localization_At(Localization_Count()) == NULL);
    assert(Localization_Find(NULL) == NULL);
    assert(Localization_Apply("en"));
    const LocalizationCatalog *second = Localization_Snapshot();
    assert(strcmp(second->code, "en") == 0);
    assert(strcmp(first->code, "tr") == 0);
    assert(strcmp(Localization_Text(second, LOC_DISPLAY_LANGUAGE), "Language") == 0);
    assert(strcmp(Localization_TextKey(second, "display.language"), "Language") == 0);
    assert(Localization_TextKey(second, "missing.key") == NULL);
    assert(Localization_TextKey(NULL, "display.language") == NULL);
    assert(Localization_TextKey(first, NULL) == NULL);
    for (int index = 0; index < LOC_MESSAGE_COUNT; ++index)
        assert(Localization_TextKey(first, localization_message_keys[index]) == first->messages[index]);
    assert(!Localization_Apply("removed"));
    assert(Localization_Snapshot() == second);
    assert(!Localization_Apply(NULL));
    assert(strcmp(Localization_Text(NULL, LOC_DISPLAY_LANGUAGE), "") == 0);
    assert(strcmp(Localization_Text(first, (LocalizationMessage)-1), "") == 0);
    assert(Localization_Apply("tr"));
    assert(Localization_Snapshot() == first);
    return 0;
}
''', encoding="utf-8")
        executable = output / ("selection.exe" if os.name == "nt" else "selection")
        build = subprocess.run([os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "include"), "-I", str(output), str(harness),
            str(ROOT / "src/Core/Localization/Localization.c"),
            str(output / "LocalizationCatalog.generated.c"), "-o", str(executable)],
            capture_output=True, text=True, timeout=30)
        self.assertEqual(0, build.returncode, build.stdout + build.stderr)
        self.assertEqual("", build.stderr)
        run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=10)
        self.assertEqual(0, run.returncode, run.stdout + run.stderr)

    def test_printf_reference_keeps_compiler_argument_type_checks(self):
        output = self.directory / "format"
        generator.generate(ROOT / "resources/translations", output, ROOT / "src")
        source = output / "format.c"
        source.write_text('''#include "Core/Localization/Localization.h"
void show(const LocalizationCatalog *catalog) {
    LOC_PRINTF(catalog, LOC_CLI_LANGUAGE_CURRENT, "Language: %s\\n", 123);
}
''')
        command = [os.environ.get("CC", "gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-fsyntax-only", "-I", str(ROOT / "include"), "-I", str(output), str(source)]
        broken = subprocess.run(command, capture_output=True, text=True, timeout=30)
        self.assertNotEqual(0, broken.returncode)
        self.assertIn("format", broken.stderr)
        source.write_text(source.read_text().replace(", 123);", ', "English");'))
        valid = subprocess.run(command, capture_output=True, text=True, timeout=30)
        self.assertEqual(0, valid.returncode, valid.stdout + valid.stderr)
        self.assertEqual("", valid.stderr)


if __name__ == "__main__":
    unittest.main()
