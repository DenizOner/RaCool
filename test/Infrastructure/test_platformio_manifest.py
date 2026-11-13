import sys
from pathlib import Path
import tempfile
import unittest
import shutil
import subprocess
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from platformio_manifest import prepare_cache, record_cache, manifest_digest, MARKER


class ManifestCacheTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.project = Path(self.directory.name)
        (self.project / "src").mkdir()
        (self.project / "tools").mkdir()
        shutil.copyfile(Path(__file__).resolve().parents[2] / "tools/product_version.py", self.project / "tools/product_version.py")
        (self.project / "src/idf_component.yml").write_text("sdk: 2.0.0")
        self.output = self.project / "build/env"
        self.output.mkdir(parents=True)
        self.cache = self.output / "CMakeCache.txt"
        self.cache.write_text("old cache")
        self.artifact = self.output / "firmware.bin"
        self.artifact.write_bytes(b"preserved")

    def test_first_use_invalidates_only_cache(self):
        prepare_cache(self.project, self.output)
        self.assertFalse(self.cache.exists())
        self.assertEqual(b"preserved", self.artifact.read_bytes())

    def test_unchanged_success_keeps_cache(self):
        record_cache(self.output, manifest_digest(self.project))
        prepare_cache(self.project, self.output)
        self.assertTrue(self.cache.exists())

    def test_revision_invalidates_cache(self):
        record_cache(self.output, manifest_digest(self.project))
        (self.project / "src/idf_component.yml").write_text("sdk: 2.0.4")
        prepare_cache(self.project, self.output)
        self.assertFalse(self.cache.exists())

    def test_failed_configuration_retries_invalidation(self):
        prepare_cache(self.project, self.output)
        self.assertFalse((self.output / MARKER).exists())
        self.cache.write_text("partial cache")
        prepare_cache(self.project, self.output)
        self.assertFalse(self.cache.exists())

    def test_environments_keep_separate_markers(self):
        other = self.project / "build/target"
        other.mkdir()
        cache = other / "CMakeCache.txt"
        cache.write_text("target")
        record_cache(self.output, manifest_digest(self.project))
        prepare_cache(self.project, other)
        self.assertFalse(cache.exists())
        prepare_cache(self.project, self.output)
        self.assertTrue(self.cache.exists())

    def test_catalog_add_edit_remove_each_invalidate_metadata(self):
        directory = self.project / "resources/translations"
        directory.mkdir(parents=True)
        catalog = directory / "bg.json"
        for action in (lambda: catalog.write_text('{"locale":"bg"}'),
                       lambda: catalog.write_text('{"locale":"bg","name":"Bulgarian"}'),
                       lambda: catalog.unlink()):
            with self.subTest(action=action):
                self.cache.write_text("old")
                record_cache(self.output, manifest_digest(self.project))
                action()
                prepare_cache(self.project, self.output)
                self.assertFalse(self.cache.exists())
                self.assertEqual(b"preserved", self.artifact.read_bytes())

    def test_source_format_contract_changes_invalidate_but_unrelated_source_does_not(self):
        consumer = self.project / "src/consumer.c"
        unrelated = self.project / "src/sensor.c"
        consumer.write_text('LOC_PRINTF(catalog, LOC_KEY, "%s", name);')
        unrelated.write_text('void sensor(void) {}')
        record_cache(self.output, manifest_digest(self.project))
        unrelated.write_text('void sensor(void) { return; }')
        prepare_cache(self.project, self.output)
        self.assertTrue(self.cache.exists())
        consumer.write_text('LOC_PRINTF(catalog, LOC_KEY, "%lu", value);')
        prepare_cache(self.project, self.output)
        self.assertFalse(self.cache.exists())

    def test_generator_and_font_revisions_invalidate_metadata(self):
        for relative in ("tools/generate_translations.py", "tools/generate_oled_fonts.mjs",
                         "resources/fonts/NotoSans-SemiBold.ttf", "package-lock.json", "resources/product-version.json"):
            with self.subTest(relative=relative):
                self.cache.write_text("old")
                record_cache(self.output, manifest_digest(self.project))
                path = self.project / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"revision")
                prepare_cache(self.project, self.output)
                self.assertFalse(self.cache.exists())

    def test_git_identity_changes_invalidate_version_metadata(self):
        (self.project / ".git").mkdir()
        state = {"dirty": False, "tag": ""}
        def git_run(command, **kwargs):
            if "rev-parse" in command:
                return subprocess.CompletedProcess(command, 0, "a" * 40 + "\n", "")
            if "diff-index" in command:
                return subprocess.CompletedProcess(command, int(state["dirty"]), "", "")
            if "ls-files" in command:
                return subprocess.CompletedProcess(command, 0, "", "")
            if "tag" in command:
                return subprocess.CompletedProcess(command, 0, state["tag"], "")
            self.fail("Unexpected Git command")
        with patch("subprocess.run", side_effect=git_run):
            record_cache(self.output, manifest_digest(self.project))
            state["tag"] = "v1.2.0\n"
            prepare_cache(self.project, self.output)
            self.assertFalse(self.cache.exists())
            self.cache.write_text("cached")
            record_cache(self.output, manifest_digest(self.project))
            state["dirty"] = True
            prepare_cache(self.project, self.output)
            self.assertFalse(self.cache.exists())


if __name__ == "__main__": unittest.main()
