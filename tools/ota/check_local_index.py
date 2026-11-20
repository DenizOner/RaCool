#!/usr/bin/env python3
"""Read-only consistency check for a single local RaCool Zigbee2MQTT OTA entry.

Structural matching is NOT RSA-v2 signature verification or permission to
publish, configure Zigbee2MQTT, flash, or install the image.
"""
import argparse
import hmac
import json
import os
import re
import stat
import struct
import sys
from pathlib import Path

from inspect_local_image import inspect_file

MAX_INDEX_BYTES = 16 * 1024
SAFE_OTA_NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}\.(?:ota|zigbee)\Z")
REQUIRED_FIELDS = frozenset({
    "manufacturerCode", "imageType", "fileVersion", "fileSize", "sha512",
    "hardwareVersionMin", "hardwareVersionMax", "modelId",
    "manufacturerName", "url", "fileName",
})
OPTIONAL_FIELDS = frozenset({"otaHeaderString"})


def _fingerprint(value):
    return (value.st_dev, value.st_ino, value.st_mode, value.st_size,
            value.st_mtime_ns, value.st_ctime_ns)


def _unique_keys(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError("duplicate index JSON key")
        result[key] = value
    return result


def _read_local_index(path):
    if path.is_symlink():
        raise ValueError("index symbolic links are forbidden")
    with path.open("rb") as source:
        before = os.fstat(source.fileno())
        if not stat.S_ISREG(before.st_mode) or not 2 <= before.st_size <= MAX_INDEX_BYTES:
            raise ValueError("index is not a bounded regular file")
        raw = source.read(MAX_INDEX_BYTES + 1)
        after = os.fstat(source.fileno())
        if len(raw) > MAX_INDEX_BYTES or len(raw) != before.st_size or _fingerprint(before) != _fingerprint(after):
            raise ValueError("index changed or exceeds limit")
    try:
        payload = json.loads(raw.decode("utf-8"), object_pairs_hook=_unique_keys,
                             parse_constant=lambda _: (_ for _ in ()).throw(ValueError("invalid JSON constant")))
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ValueError("invalid UTF-8 JSON index") from error
    if type(payload) is not list or len(payload) != 1 or type(payload[0]) is not dict:
        raise ValueError("index must contain exactly one RaCool image")
    entry = payload[0]
    if not REQUIRED_FIELDS.issubset(entry) or set(entry) - REQUIRED_FIELDS - OPTIONAL_FIELDS:
        raise ValueError("missing or unsupported OTA index fields")
    return entry, _fingerprint(before)


def validate_index(index_path, root, previous_version=None):
    """Validate an existing local index + sibling OTA file without writing either."""
    index_path = Path(index_path)
    entry, original_index = _read_local_index(index_path)
    name = entry["url"]
    if (type(name) is not str or SAFE_OTA_NAME.fullmatch(name) is None or
            type(entry["fileName"]) is not str or entry["fileName"] != name):
        raise ValueError("OTA URL must equal a local sibling file name")
    if entry["modelId"] != "RaCool-ESP32C6" or entry["manufacturerName"] != ["RaCool"]:
        raise ValueError("index device targeting differs from RaCool")
    image_path = index_path.parent / name
    if image_path.is_symlink():
        raise ValueError("OTA image symbolic links are forbidden")
    inspected = inspect_file(image_path, root, previous_version)
    expected = {
        "manufacturerCode": inspected["manufacturerCode"],
        "imageType": inspected["imageType"],
        "fileVersion": inspected["fileVersion"],
        "fileSize": inspected["fileSize"],
        "hardwareVersionMin": inspected["hardwareMin"],
        "hardwareVersionMax": inspected["hardwareMax"],
    }
    for field, value in expected.items():
        if type(entry[field]) is not int or entry[field] != value:
            raise ValueError("index value does not match OTA image: " + field)
    checksum = entry["sha512"]
    if (type(checksum) is not str or re.fullmatch(r"[0-9a-f]{128}", checksum) is None or
            not hmac.compare_digest(checksum, inspected["sha512"])):
        raise ValueError("index SHA-512 does not match OTA image")
    if "otaHeaderString" in entry:
        if (type(entry["otaHeaderString"]) is not str or
                entry["otaHeaderString"] != inspected["otaHeaderString"]):
            raise ValueError("index OTA header string mismatch")
    if index_path.is_symlink() or _fingerprint(index_path.stat()) != original_index:
        raise ValueError("index changed during image inspection")
    return {
        "status": "LOCAL_INDEX_MATCH_SIGNATURE_UNVERIFIED",
        "signature_verified": False,
        "installation_authorized": False,
        "index_matches_image": True,
        "upgrade_order_verified": inspected["upgrade_order_verified"],
        "fileName": name,
        "fileVersion": inspected["fileVersion"],
        "fileSize": inspected["fileSize"],
        "appVersion": inspected["appVersion"],
        "sha512": inspected["sha512"],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("index", type=Path, help="Existing local Zigbee2MQTT JSON index")
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--previous-ota-version", type=lambda value: int(value, 0),
                        help="Verified previous fileVersion; never guessed")
    args = parser.parse_args(argv)
    try:
        print(json.dumps(validate_index(args.index, args.repo_root,
                                        args.previous_ota_version), sort_keys=True))
        return 0
    except (OSError, ValueError, KeyError, TypeError, UnicodeError, struct.error) as error:
        # Do not reveal the operator's local Zigbee2MQTT path or secrets.
        message = str(error) if isinstance(error, ValueError) else "input unavailable or invalid"
        print("REJECTED: " + message, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
