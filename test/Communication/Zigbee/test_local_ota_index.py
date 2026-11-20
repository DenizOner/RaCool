#!/usr/bin/env python3
"""Fail-closed checks of a Zigbee2MQTT-local RaCool OTA index (no publishing)."""
import json
import os
import stat
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/ota"))
import inspect_local_image as image_checker  # noqa: E402
import check_local_index as index_checker  # noqa: E402


class LocalIndexTests(unittest.TestCase):
    def setUp(self):
        self.policy = image_checker.load_policy(ROOT)
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.parent = Path(self.directory.name)
        self.image_path = self.parent / "racool-ota.zigbee"
        self.index_path = self.parent / "racool-index.json"
        app = bytearray(128)
        app[0] = image_checker.APP_MAGIC
        app[32:36] = image_checker.APP_DESCRIPTOR_MAGIC
        version = self.policy["app_version"].encode("ascii")
        app[48:48 + len(version)] = version
        total = 60 + 6 + len(app)
        header = image_checker.OTA_HEADER.pack(
            image_checker.FILE_MAGIC, image_checker.HEADER_VERSION, 60, 4,
            self.policy["manufacturer"], self.policy["image_type"],
            self.policy["candidate_version"], 2, bytes(32), total)
        self.image_path.write_bytes(header + struct.pack("<HH", self.policy["hardware_version"],
                                     self.policy["hardware_version"]) +
                                    image_checker.OTA_TAG.pack(0, len(app)) + app)
        self.inspected = image_checker.inspect_file(self.image_path, ROOT)
        self.entry = {
            "manufacturerCode": self.inspected["manufacturerCode"],
            "imageType": self.inspected["imageType"],
            "fileVersion": self.inspected["fileVersion"],
            "fileSize": self.inspected["fileSize"],
            "sha512": self.inspected["sha512"],
            "hardwareVersionMin": self.inspected["hardwareMin"],
            "hardwareVersionMax": self.inspected["hardwareMax"],
            "modelId": "RaCool-ESP32C6",
            "manufacturerName": ["RaCool"],
            "url": self.image_path.name,
            "fileName": self.image_path.name,
        }
        self.save()

    def save(self, entries=None):
        self.index_path.write_text(json.dumps([self.entry] if entries is None else entries),
                                   encoding="utf-8")

    def assert_rejected(self):
        with self.assertRaises((ValueError, OSError)):
            index_checker.validate_index(self.index_path, ROOT)

    def test_matches_sibling_image_but_never_authorizes_install(self):
        before_index = self.index_path.read_bytes()
        before_image = self.image_path.read_bytes()
        result = index_checker.validate_index(self.index_path, ROOT)
        self.assertEqual("LOCAL_INDEX_MATCH_SIGNATURE_UNVERIFIED", result["status"])
        self.assertTrue(result["index_matches_image"])
        self.assertFalse(result["signature_verified"])
        self.assertFalse(result["installation_authorized"])
        self.assertFalse(result["upgrade_order_verified"])
        self.assertEqual(self.inspected["sha512"], result["sha512"])
        self.assertEqual(before_index, self.index_path.read_bytes())
        self.assertEqual(before_image, self.image_path.read_bytes())

    def test_previous_counter_is_verified_only_when_supplied(self):
        result = index_checker.validate_index(self.index_path, ROOT,
                              self.policy["candidate_version"] - 1)
        self.assertTrue(result["upgrade_order_verified"])
        self.assertFalse(result["installation_authorized"])
        with self.assertRaises(ValueError):
            index_checker.validate_index(self.index_path, ROOT,
                                         self.policy["candidate_version"])

    def test_rejects_forged_digest_and_case(self):
        for value in ("0" * 128, self.entry["sha512"].upper(), "g" * 128,
                      self.entry["sha512"][:-1], None):
            with self.subTest(value=str(value)[:10]):
                self.entry["sha512"] = value
                self.save()
                self.assert_rejected()

    def test_rejects_mismatched_integer_fields_and_bool(self):
        for field in ("manufacturerCode", "imageType", "fileVersion", "fileSize",
                      "hardwareVersionMin", "hardwareVersionMax"):
            for value in (self.entry[field] + 1, True, str(self.entry[field])):
                with self.subTest(field=field, value=value):
                    old = self.entry[field]
                    self.entry[field] = value
                    self.save()
                    self.assert_rejected()
                    self.entry[field] = old

    def test_rejects_other_model_and_manufacturer(self):
        for field, value in (("modelId", "Another-Device"),
                             ("manufacturerName", ["RaCool", "Other"]),
                             ("manufacturerName", "RaCool"),
                             ("manufacturerName", [])):
            with self.subTest(field=field, value=value):
                old = self.entry[field]
                self.entry[field] = value
                self.save()
                self.assert_rejected()
                self.entry[field] = old

    def test_rejects_remote_absolute_parent_and_escaped_urls(self):
        for value in ("../racool-ota.zigbee", "dir/racool-ota.zigbee",
                      "sub\\racool-ota.zigbee", "/tmp/racool-ota.zigbee",
                      "C:\\racool-ota.zigbee", "file:///tmp/racool-ota.zigbee",
                      "https://example.com/file.zigbee", "%2e%2e.zigbee",
                      "?file.zigbee", "", 12):
            with self.subTest(value=value):
                self.entry["url"] = value
                self.entry["fileName"] = value
                self.save()
                self.assert_rejected()

    def test_rejects_file_name_mismatch(self):
        self.entry["fileName"] = "other.zigbee"
        self.save()
        self.assert_rejected()

    def test_rejects_missing_image_without_writing(self):
        self.image_path.unlink()
        self.assert_rejected()
        self.assertTrue(self.index_path.is_file())

    def test_rejects_modified_or_truncated_image_even_with_old_index(self):
        with self.image_path.open("ab") as stream:
            stream.write(b"x")
        self.assert_rejected()

    def test_rejects_extra_entries_and_nonobject_index(self):
        for entries in ([], [self.entry, self.entry], [None], {"url": self.image_path.name}):
            with self.subTest(entries=str(entries)[:30]):
                self.save(entries)
                self.assert_rejected()

    def test_rejects_duplicate_keys_force_and_unapproved_metadata(self):
        self.index_path.write_text('[{"url":"a.zigbee","url":"b.zigbee"}]', encoding="utf-8")
        self.assert_rejected()
        for key, value in (("force", True), ("maxFileVersion", 99),
                           ("releaseNotes", "arbitrary")):
            with self.subTest(key=key):
                self.entry[key] = value
                self.save()
                self.assert_rejected()
                del self.entry[key]

    def test_rejects_missing_required_fields(self):
        for key in tuple(self.entry):
            with self.subTest(key=key):
                value = self.entry.pop(key)
                self.save()
                self.assert_rejected()
                self.entry[key] = value

    def test_rejects_oversized_invalid_and_nonfinite_json(self):
        self.index_path.write_bytes(b" " * (index_checker.MAX_INDEX_BYTES + 1))
        self.assert_rejected()
        for blob in (b"\xff", b"[", b"[NaN]", b"[Infinity]", b"null"):
            with self.subTest(blob=blob):
                self.index_path.write_bytes(blob)
                self.assert_rejected()

    def test_optional_header_string_must_equal_raw_header_field(self):
        self.entry["otaHeaderString"] = "\0" * 32
        self.save()
        self.assertTrue(index_checker.validate_index(self.index_path, ROOT)["index_matches_image"])
        self.entry["otaHeaderString"] = "fake"
        self.save()
        self.assert_rejected()

    def test_rejects_image_and_index_symlinks(self):
        alias = self.parent / "alias.zigbee"
        try:
            alias.symlink_to(self.image_path)
        except (OSError, NotImplementedError):
            self.skipTest("symlinks not supported on this runner")
        self.entry["url"] = alias.name
        self.entry["fileName"] = alias.name
        self.save()
        self.assert_rejected()
        self.entry["url"] = self.image_path.name
        self.entry["fileName"] = self.image_path.name
        self.save()
        other_index = self.parent / "other-index.json"
        other_index.symlink_to(self.index_path)
        with self.assertRaises(ValueError):
            index_checker.validate_index(other_index, ROOT)

    def test_rejects_detected_index_change_during_image_read(self):
        original = index_checker.inspect_file
        def mutate_then_check(*args):
            result = original(*args)
            self.index_path.write_bytes(self.index_path.read_bytes() + b" ")
            return result
        with patch.object(index_checker, "inspect_file", side_effect=mutate_then_check):
            self.assert_rejected()

    def test_cli_reports_no_publish_or_signature_success(self):
        import subprocess
        process = subprocess.run([sys.executable,
                                  str(ROOT / "tools/ota/check_local_index.py"),
                                  str(self.index_path), "--repo-root", str(ROOT)],
                                 capture_output=True, text=True, check=False)
        self.assertEqual(0, process.returncode, process.stderr)
        result = json.loads(process.stdout)
        self.assertFalse(result["installation_authorized"])
        self.assertFalse(result["signature_verified"])
        self.entry["url"] = "https://example.com/ota.zigbee"
        self.save()
        failure = subprocess.run([sys.executable,
                                  str(ROOT / "tools/ota/check_local_index.py"),
                                  str(self.index_path), "--repo-root", str(ROOT)],
                                 capture_output=True, text=True, check=False)
        self.assertNotEqual(0, failure.returncode)
        self.assertIn("REJECTED", failure.stderr)
        self.assertNotIn(str(self.parent), failure.stderr)


if __name__ == "__main__":
    unittest.main()
