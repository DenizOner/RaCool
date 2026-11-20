#!/usr/bin/env python3
"""Negative acceptance tests for the read-only RaCool OTA envelope inspector."""
import importlib.util
import json
import os
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
SCRIPT = ROOT / "tools/ota/inspect_local_image.py"
OFFICIAL_BUILDER = ROOT / "tools/ota/vendor/esp_zigbee_sdk_2_0_5_image_builder.py"
BUILT_PREFLIGHT = ROOT / "tools/ota/check_compiled_candidate.py"
spec = importlib.util.spec_from_file_location("inspect_local_image", SCRIPT)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)
built_spec = importlib.util.spec_from_file_location("check_compiled_candidate", BUILT_PREFLIGHT)
built_checker = importlib.util.module_from_spec(built_spec)
sys.path.insert(0, str(BUILT_PREFLIGHT.parent))
built_spec.loader.exec_module(built_checker)


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
            version = p["app_version"].encode("ascii")
            app[48:48 + len(version)] = version
        total = 60 + 6 + len(app)
        args = dict(magic=checker.FILE_MAGIC, version=checker.HEADER_VERSION,
                    header_len=60, flags=4, manufacturer=p["manufacturer"],
                    image_type=p["image_type"], file_version=p["candidate_version"],
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
        self.assertEqual(self.policy["app_version"], result["appVersion"])
        self.assertTrue(result["source_version_matched"])
        self.assertFalse(result["upgrade_order_verified"])

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

    def test_rejects_counter_different_from_source_candidate(self):
        for value in (0, self.policy["candidate_version"] - 1,
                      self.policy["candidate_version"] + 1, 0xFFFFFFFF):
            with self.subTest(version=value):
                self.check_rejects(self.fixture(file_version=value))

    def test_previous_counter_is_optional_but_cannot_authorize_reuse(self):
        candidate = self.policy["candidate_version"]
        result = checker.inspect_structure(self.fixture(), self.policy, candidate - 1)
        self.assertTrue(result["upgrade_order_verified"])
        self.assertFalse(result["installation_authorized"])
        for old in (candidate, candidate + 1, 0xFFFFFFFF, -1, True, "0"):
            with self.subTest(old=old), self.assertRaises(ValueError):
                checker.inspect_structure(self.fixture(), self.policy, old)

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


    def test_source_descriptor_must_match_exact_semver_identity(self):
        wrong = bytearray(self.fixture())
        wrong[114:146] = b"2.0.0" + bytes(27)
        self.check_rejects(wrong)

    def test_official_sdk_builder_produces_the_same_exact_ota_envelope(self):
        """Call the unmodified, pinned Espressif builder, not a local reimplementation."""
        p = self.policy
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            app = out / "synthetic-unsigned-app.bin"
            app.write_bytes(self.fixture()[66:])
            command = [
                sys.executable, str(OFFICIAL_BUILDER),
                "--manuf-id", hex(p["manufacturer"]),
                "--image-type", hex(p["image_type"]),
                "--file-version", hex(p["candidate_version"]),
                "--min-hw-ver", str(p["hardware_version"]),
                "--max-hw-ver", str(p["hardware_version"]),
                "--tag", "0", str(app),
            ]
            run = subprocess.run(command, cwd=out, capture_output=True,
                                 text=True, check=False)
            self.assertEqual(0, run.returncode, run.stderr)
            outputs = list(out.glob("*-ota-file.zigbee"))
            self.assertEqual(1, len(outputs))
            actual = outputs[0].read_bytes()
            self.assertEqual(self.fixture(), actual)
            parsed = checker.inspect_file(outputs[0], ROOT, p["candidate_version"] - 1)
            self.assertTrue(parsed["source_version_matched"])
            self.assertTrue(parsed["upgrade_order_verified"])
            self.assertFalse(parsed["signature_verified"])
            self.assertFalse(parsed["installation_authorized"])

    def test_file_reader_never_requests_more_than_one_byte_above_limit(self):
        p = self.policy
        with tempfile.TemporaryDirectory() as directory:
            file_path = Path(directory) / "sample.ota"
            file_path.write_bytes(self.fixture())
            requests = []

            class GrowingStream:
                def __init__(self, handle):
                    self.handle = handle

                def __enter__(self):
                    return self

                def __exit__(self, *_):
                    return None

                def fileno(self):
                    return self.handle.fileno()

                def read(self, count):
                    requests.append(count)
                    return bytes(count)

            with file_path.open("rb") as handle:
                with patch.object(checker, "load_policy", return_value=p):
                    with patch.object(Path, "open", return_value=GrowingStream(handle)):
                        with self.assertRaisesRegex(ValueError, "grew"):
                            checker.inspect_file(file_path, ROOT)
            self.assertEqual([p["slot_size"] + 67], requests)

    def test_file_reader_rejects_nonregular_and_during_read_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            file_path = Path(directory) / "sample.ota"
            file_path.write_bytes(self.fixture())
            baseline = os.stat(file_path)
            not_regular = SimpleNamespace(st_mode=stat.S_IFIFO, st_size=baseline.st_size)
            changed = SimpleNamespace(st_mode=baseline.st_mode, st_dev=baseline.st_dev,
                                      st_ino=baseline.st_ino, st_size=baseline.st_size,
                                      st_mtime_ns=baseline.st_mtime_ns + 1,
                                      st_ctime_ns=baseline.st_ctime_ns)
            with patch.object(checker, "load_policy", return_value=self.policy):
                with patch.object(checker.os, "fstat", return_value=not_regular):
                    with self.assertRaisesRegex(ValueError, "regular file"):
                        checker.inspect_file(file_path, ROOT)
                with patch.object(checker.os, "fstat", side_effect=[baseline, changed]):
                    with self.assertRaisesRegex(ValueError, "changed during inspection"):
                        checker.inspect_file(file_path, ROOT)


    def test_compiled_candidate_matches_source_version_without_authorizing_install(self):
        import hashlib
        app = self.fixture()[66:]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.bin"
            path.write_bytes(app)
            report = built_checker.check_compiled_candidate(path, ROOT)
            self.assertEqual("COMPILED_APP_SOURCE_MATCH_SIGNATURE_UNVERIFIED", report["status"])
            self.assertTrue(report["source_version_matched"])
            self.assertEqual(self.policy["app_version"], report["appVersion"])
            self.assertEqual(self.policy["candidate_version"], report["otaFileVersion"])
            self.assertEqual(len(app), report["fileSize"])
            self.assertEqual(self.policy["slot_size"], report["otaSlotCapacity"])
            self.assertEqual(hashlib.sha256(app).hexdigest(), report["sha256"])
            self.assertFalse(report["signature_verified"])
            self.assertFalse(report["installation_authorized"])
            self.assertEqual(app, path.read_bytes())

    def test_compiled_candidate_rejects_corrupted_esp_magic_descriptor_and_version(self):
        app = self.fixture()[66:]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.bin"
            for index, value in ((0, 0), (32, 0), (50, 0xff)):
                wrong = bytearray(app)
                wrong[index] = value
                path.write_bytes(wrong)
                with self.subTest(index=index), self.assertRaises(ValueError):
                    built_checker.check_compiled_candidate(path, ROOT)

    def test_compiled_candidate_rejects_other_valid_semver_from_source(self):
        wrong = bytearray(self.fixture()[66:])
        wrong[48:80] = b"2.0.0" + bytes(27)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.bin"
            path.write_bytes(wrong)
            with self.assertRaisesRegex(ValueError, "differs from source"):
                built_checker.check_compiled_candidate(path, ROOT)

    def test_compiled_candidate_rejects_short_missing_and_oversized_files(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.bin"
            path.write_bytes(b"\xe9" * 79)
            with self.assertRaisesRegex(ValueError, "outside OTA"):
                built_checker.check_compiled_candidate(path, ROOT)
            with path.open("wb") as handle:
                handle.truncate(self.policy["slot_size"] + 1)
            with self.assertRaisesRegex(ValueError, "outside OTA"):
                built_checker.check_compiled_candidate(path, ROOT)
            with self.assertRaises(OSError):
                built_checker.check_compiled_candidate(Path(directory) / "missing.bin", ROOT)

    def test_compiled_candidate_rejects_detected_file_change_without_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.bin"
            path.write_bytes(self.fixture()[66:])
            baseline = path.stat()
            modified = SimpleNamespace(st_dev=baseline.st_dev, st_ino=baseline.st_ino,
                                       st_size=baseline.st_size,
                                       st_mtime_ns=baseline.st_mtime_ns + 1,
                                       st_ctime_ns=baseline.st_ctime_ns)
            with patch.object(built_checker.os, "fstat", side_effect=[baseline, modified]):
                with self.assertRaisesRegex(ValueError, "changed during inspection"):
                    built_checker.check_compiled_candidate(path, ROOT)

    def test_compiled_candidate_cli_fail_closed_and_no_file_modifications(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.bin"
            image = self.fixture()[66:]
            path.write_bytes(image)
            command = [sys.executable, str(BUILT_PREFLIGHT), "--firmware", str(path),
                       "--repo-root", str(ROOT)]
            passed = subprocess.run(command, text=True, capture_output=True, check=False)
            self.assertEqual(0, passed.returncode, passed.stderr)
            parsed = json.loads(passed.stdout)
            self.assertFalse(parsed["signature_verified"])
            self.assertFalse(parsed["installation_authorized"])
            self.assertEqual(image, path.read_bytes())
            path.write_bytes(b"bad")
            failed = subprocess.run(command, text=True, capture_output=True, check=False)
            self.assertNotEqual(0, failed.returncode)
            self.assertIn("REJECTED:", failed.stderr)
            self.assertNotIn(str(path), failed.stderr)

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
            self.assertTrue(parsed["source_version_matched"])
            self.assertFalse(parsed["upgrade_order_verified"])
            ordered = subprocess.run(args + ["--previous-ota-version",
                                              hex(self.policy["candidate_version"] - 1)],
                                     capture_output=True, text=True, check=False)
            self.assertEqual(0, ordered.returncode, ordered.stderr)
            self.assertTrue(json.loads(ordered.stdout)["upgrade_order_verified"])
            reused = subprocess.run(args + ["--previous-ota-version",
                                             hex(self.policy["candidate_version"])],
                                    capture_output=True, text=True, check=False)
            self.assertNotEqual(0, reused.returncode)
            self.assertEqual(blob, path.read_bytes())
            path.write_bytes(b"invalid")
            rejected = subprocess.run(args, capture_output=True, text=True, check=False)
            self.assertNotEqual(0, rejected.returncode)
            self.assertIn("REJECTED", rejected.stderr)
            self.assertNotIn(str(path), rejected.stderr)


if __name__ == "__main__":
    unittest.main()
