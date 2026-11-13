import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("product_version", ROOT / "tools/product_version.py")
version = importlib.util.module_from_spec(spec)
spec.loader.exec_module(version)


class ProductVersionTests(unittest.TestCase):
    def test_semver_rejects_invalid_numeric_identifiers(self):
        for value in ("01.2.3", "1.2.3.4", "1.2.3-01", "1.2", "1.2.3+"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                version.parse_version(value)
        self.assertEqual(((1, 2, 3), "dev.7", "g123"), version.parse_version("1.2.3-dev.7+g123"))

    def test_classification_uses_largest_change(self):
        self.assertEqual("1.2.4", version.next_version("1.2.3", ["fix"]))
        self.assertEqual("1.3.0", version.next_version("1.2.3", ["feat", "fix"]))
        self.assertEqual("2.0.0", version.next_version("1.2.3", ["breaking", "feat"]))
        for changes in ([], ["unknown"]):
            with self.assertRaises(ValueError): version.next_version("1.2.3", changes)

    def test_prerelease_precedence_ignores_build_metadata(self):
        ordered = ["1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                   "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0"]
        for low, high in zip(ordered, ordered[1:]):
            self.assertLess(version.compare_versions(low, high), 0)
            self.assertGreater(version.compare_versions(high, low), 0)
        self.assertEqual(0, version.compare_versions("1.0.0+a", "1.0.0+b"))

    def test_zigbee_basic_version_has_a_sixteen_byte_limit(self):
        self.assertEqual("1.2.0-dev.0", version.zigbee_version("1.2.0-dev.0+gabcdef.dirty"))
        self.assertEqual("1.2.0", version.zigbee_version("1.2.0+build"))
        with self.assertRaises(ValueError): version.zigbee_version("1234567890.123.1-dev.0")

    def test_ota_downgrade_collision_overflow_and_prerelease_rejected(self):
        a, b = "a" * 64, "b" * 64
        version.check_release("1.2.3", "1.2.2", 11, 10, b, a, "stable")
        version.check_release("1.2.3", "1.2.3", 10, 10, a, a, "stable")
        for args in (("1.2.3", "1.2.2", 9, 10, a, a, "stable"), ("1.2.3", "1.2.2", 10, 10, b, a, "stable"),
                     ("1.2.3", "1.2.2", 0x100000000, 10, a, a, "stable"), ("1.2.3-dev.1", "1.2.2", 11, 10, b, a, "stable"),
                     ("1.2.3+other", "1.2.3+old", 11, 10, b, a, "stable"), ("1.2.2", "1.2.3", 11, 10, b, a, "stable")):
            with self.subTest(args=args), self.assertRaises(ValueError): version.check_release(*args)

    def test_release_plan_requires_ordered_history_and_described_changes(self):
        a, b = "a" * 64, "b" * 64
        history = [{"version": "1.2.0", "ota_code": 10, "sha256": a}]
        changes = [{"type": "feat", "summary": "New setting", "commit": "c" * 40}]
        self.assertEqual("1.3.0", version.plan_release(history, changes, 11)["version"])
        for h, c, code in (([], changes, 11), (history, [{"type": "feat", "summary": "", "commit": "c" * 40}], 11),
                           (history, [{"type": "other", "summary": "x", "commit": "c" * 40}], 11), (history, changes, 10),
                           (history + [{"version": "1.1.0", "ota_code": 11, "sha256": b}], changes, 12)):
            with self.subTest(history=h, changes=c, code=code), self.assertRaises(ValueError):
                version.plan_release(h, c, code)

    def test_release_plan_rejects_unclassified_or_duplicate_commits(self):
        class Result:
            def __init__(self, code, stdout=""):
                self.returncode, self.stdout = code, stdout
        a, b = "a" * 40, "b" * 40
        changes = [{"commit": a}, {"commit": b}]
        with patch.object(version.subprocess, "run", side_effect=[Result(0), Result(0, a + "\n" + b + "\n")]):
            version.check_commit_coverage(ROOT, "1.2.0", changes)
        for supplied in ([{"commit": a}], [{"commit": a}, {"commit": a}]):
            with patch.object(version.subprocess, "run", side_effect=[Result(0), Result(0, a + "\n" + b + "\n")]), self.assertRaises(ValueError):
                version.check_commit_coverage(ROOT, "1.2.0", supplied)
        with patch.object(version.subprocess, "run", return_value=Result(1)), self.assertRaises(ValueError):
            version.check_commit_coverage(ROOT, "1.2.0", changes)

    def test_clean_release_dirty_and_archive_identity(self):
        manifest = {"base_version": "1.2.0", "ota_file_version": 0x01020000, "archive_identity": "archive"}
        with patch.object(version, "git_identity", return_value=("abcdef123456", False, ["v1.2.0"])):
            self.assertEqual("1.2.0", version.identity(manifest, ROOT)["full"])
        with patch.object(version, "git_identity", return_value=("abcdef123456", True, ["v1.2.0"])):
            self.assertEqual("1.2.0-dev.0+gabcdef123456.dirty", version.identity(manifest, ROOT)["full"])
        with patch.object(version, "git_identity", return_value=None):
            self.assertEqual("1.2.0-dev.0+archive", version.identity(manifest, ROOT)["full"])
            with self.assertRaises(ValueError): version.identity({**manifest, "archive_identity": ""}, ROOT)

    def test_unchanged_generated_file_is_not_rewritten(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "version.h"
            version.write_changed(path, "same\n")
            before = path.stat().st_mtime_ns
            version.write_changed(path, "same\n")
            self.assertEqual(before, path.stat().st_mtime_ns)

    def test_release_image_identity_comes_from_actual_descriptor(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "firmware.bin"
            raw = bytearray(128)
            raw[32:36] = bytes.fromhex("3254cdab")
            raw[48:53] = b"1.2.3"
            image.write_bytes(raw)
            actual, digest = version.inspect_image(image)
            self.assertEqual("1.2.3", actual)
            self.assertEqual(64, len(digest))
            raw[32] = 0
            image.write_bytes(raw)
            with self.assertRaises(ValueError): version.inspect_image(image)

    def test_archive_inside_unrelated_git_tree_never_uses_parent_identity(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(version.subprocess, "run") as run:
            self.assertIsNone(version.git_identity(directory))
            run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
