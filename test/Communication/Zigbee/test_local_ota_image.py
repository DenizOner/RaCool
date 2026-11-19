#!/usr/bin/env python3
"""Negative acceptance tests for the read-only RaCool OTA envelope inspector."""
import importlib.util
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "tools/ota/inspect_local_image.py"
spec = importlib.util.spec_from_file_location("inspect_local_image", SCRIPT)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class LocalOtaEnvelopeTests(unittest.TestCase):
    def setUp(self):
        self.policy = checker.load_policy(ROOT)

    def fixture(self, **options):
        p = self.policy
        app_size = options.pop("app_size", 128)
        app = bytearray(app_size)
        if app_size >= 80:
            app[0] = 0xE9
            app[32:36] = bytes.fromhex("3254cdab")
            app[48:53] = b"1.0.1"
        total = 60 + 6 + len(app)
        args = dict(magic=checker.FILE_MAGIC, version=checker.HEADER_VERSION,
                    header_len=60, flags=4, manufacturer=p["manufacturer"],
                    image_type=p["image_type"], file_version=p["current_version"] + 1,
                    stack=2, total=total, low_hw=p["hardware_version"],
                    high_hw=p["hardware_version"], tag_id=0,
                    tag_length=len(app))
        args.update(options)
        header = checker.OTA_HEADER.pack(
            args["magic"], args["version"], args["header_len"], args["flags"],
            args["manufacturer"], args["image_type"], args["file_version"],
            args["stack"], bytes(32), args["total"])
        return (header + struct.pack("<HH", args["low_hw"], args["high_hw"]) +
                checker.OTA_TAG.pack(args["tag_id"], args["tag_length"]) + app)

    def check_rejects(self, blob):
        with self.assertRaises(ValueError):
            checker.inspect_structure(bytes(blob), self.policy)

    def test_valid_envelope_is_explicitly_not_signature_verified_or_installable(self):
        result = checker.inspect_structure(self.fixture(), self.policy)
        self.assertEqual("STRUCTURE_PASS_SIGNATURE_UNVERIFIED", result["status"])
        self.assertFalse(result["signature_verified"])
        self.assertFalse(result["installation_authorized"])
        self.assertEqual(self.policy["manufacturer"], result["manufacturerCode"])
        self.assertEqual(self.policy["image_type"], result["imageType"])
        self.assertEqual(128 + 66, result["fileSize"])
        self.assertEqual(128, len(result["sha512"]))
        self.assertEqual("1.0.1", result["appVersion"])

    def test_rejects_foreign_legacy_and_wildcard_manufacturers(self):
        for code in (0x1234, 0xFFFF, 0xFFF2, 0x0000):
            with self.subTest(code=code):
                self.check_rejects(self.fixture(manufacturer=code))

    def test_rejects_wrong_image_type(self):
        self.check_rejects(self.fixture(image_type=0xFFFF))
        self.check_rejects(self.fixture(image_type=0x1235))

    def test_rejects_invalid_file_magic_version_flags_stack_and_header_length(self):
        for key, value in (("magic", 0), ("version", 0), ("flags", 0),
                           ("flags", 0x0005), ("stack", 1),
                           ("header_len", 56), ("header_len", 64)):
            with self.subTest(key=key, value=value):
                self.check_rejects(self.fixture(**{key: value}))

    def test_rejects_same_old_and_wildcard_file_versions(self):
        for value in (0, self.policy["current_version"],
                      self.policy["current_version"] - 1, 0xFFFFFFFF):
            with self.subTest(version=value):
                self.check_rejects(self.fixture(file_version=value))

    def test_rejects_invalid_or_unrelated_hardware_ranges(self):
        hardware = self.policy["hardware_version"]
        for low, high in ((hardware + 1, hardware + 2),
                          (0, hardware - 1), (hardware + 1, hardware)):
            with self.subTest(low=low, high=high):
                self.check_rejects(self.fixture(low_hw=low, high_hw=high))

    def test_rejects_truncated_extra_and_misreported_total(self):
        valid = self.fixture()
        self.check_rejects(valid[:-1])
        self.check_rejects(valid + b"\0")
        self.check_rejects(self.fixture(total=len(valid) + 1))
        self.check_rejects(self.fixture(total=len(valid) - 1))

    def test_rejects_wrong_tag_and_extra_sub_elements(self):
        self.check_rejects(self.fixture(tag_id=1))
        self.check_rejects(self.fixture(tag_length=127))
        self.check_rejects(self.fixture(tag_length=129))
        self.check_rejects(self.fixture(tag_length=0))

    def test_rejects_non_esp_payload_and_corrupt_descriptor(self):
        valid = bytearray(self.fixture())
        for index in (66, 98):
            damaged = valid[:]
            damaged[index] = 0
            self.check_rejects(damaged)
        self.check_rejects(self.fixture(app_size=79))

    def test_rejects_invalid_app_version(self):
        bad = bytearray(self.fixture())
        bad[114:119] = b"1.a.1"
        self.check_rejects(bad)
        bad = bytearray(self.fixture())
        bad[114:119] = b"\xff\xfe\xfd\xfc\xfb"
        self.check_rejects(bad)

    def test_rejects_oversize_app_even_with_matching_envelope(self):
        self.check_rejects(self.fixture(app_size=self.policy["slot_size"] + 1))

    def test_structural_validation_is_not_cryptographic_integrity(self):
        original = self.fixture()
        tampered = bytearray(original)
        tampered[-1] ^= 0x55
        before = checker.inspect_structure(original, self.policy)
        after = checker.inspect_structure(bytes(tampered), self.policy)
        self.assertNotEqual(before["sha512"], after["sha512"])
        self.assertFalse(after["signature_verified"])
        self.assertFalse(after["installation_authorized"])

    def test_rejects_invalid_policy_source_and_partition_map(self):
        self.assertEqual(0x300000, checker._slot_size((ROOT / "partitions.csv").read_text()))
        for text in ("app0,app,ota_0,0x20000,0x300000,\n",
                     "app0,app,ota_0,0x20000,0x300000,\napp1,app,ota_0,0x320000,0x300000,"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                checker._slot_size(text)
        with self.assertRaises(ValueError):
            checker._constant("#define OTHER 1\n", "ZIGBEE_OTA_MANUFACTURER_CODE")
        with self.assertRaises(ValueError):
            checker.inspect_structure(self.fixture(), {**self.policy, "slot_size": 0})

    def test_read_only_file_check_and_length_guard(self):
        blob = self.fixture()
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "local.ota"
            path.write_bytes(blob)
            self.assertEqual(len(blob), checker.inspect_file(path, ROOT)["fileSize"])
            self.assertEqual(blob, path.read_bytes())
            path.write_bytes(b"\xff" * (self.policy["slot_size"] + 67))
            with self.assertRaises(ValueError):
                checker.inspect_file(path, ROOT)

    def test_cli_reports_metadata_only_without_mutating_file(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "sample.ota"
            blob = self.fixture()
            path.write_bytes(blob)
            args = [sys.executable, str(SCRIPT), str(path), "--repo-root", str(ROOT)]
            result = subprocess.run(args, capture_output=True, text=True, check=False)
            self.assertEqual(0, result.returncode, result.stderr)
            parsed = json.loads(result.stdout)
            self.assertFalse(parsed["installation_authorized"])
            self.assertFalse(parsed["signature_verified"])
            self.assertEqual(blob, path.read_bytes())
            path.write_bytes(b"invalid")
            rejected = subprocess.run(args, capture_output=True, text=True, check=False)
            self.assertNotEqual(0, rejected.returncode)
            self.assertIn("REJECTED", rejected.stderr)
            self.assertNotIn(str(path), rejected.stderr)


if __name__ == "__main__":
    unittest.main()
