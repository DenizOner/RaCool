#!/usr/bin/env python3
"""Read-only structural inspection of a private RaCool Zigbee OTA container.

This is NOT a firmware signature verifier or release authorization.
Only ESP-IDF's actual RSA-v2 signature verification under a trusted signed
base, followed by approved device testing, can authorize installation.
"""
import argparse
import hashlib
import json
import os
import re
import stat
import struct
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
from product_version import identity, parse_version  # noqa: E402

OTA_HEADER = struct.Struct("<IHHHHHIH32sI")
OTA_TAG = struct.Struct("<HI")
FILE_MAGIC = 0x0BEEF11E
HEADER_VERSION = 0x0100
HEADER_SIZE = 60
HARDWARE_FIELD = 0x0004
STACK_VERSION = 2
APP_MAGIC = 0xE9
APP_DESCRIPTOR_MAGIC = bytes.fromhex("3254cdab")


def _constant(source, name):
    pattern = rf"^#define[ \t]+{re.escape(name)}[ \t]+(0[xX][0-9A-Fa-f]+|[0-9]+)[uUlL]*[ \t]*(?://.*)?$"
    match = re.search(pattern, source, re.MULTILINE)
    if not match:
        raise ValueError("missing or ambiguous config constant: " + name)
    return int(match.group(1), 0)


def _slot_size(partitions):
    sizes = {}
    for row in partitions.splitlines():
        row = row.strip()
        if not row or row.startswith("#"):
            continue
        fields = [item.strip() for item in row.split(",")]
        if len(fields) < 5:
            raise ValueError("invalid partition row")
        if fields[2] in ("ota_0", "ota_1"):
            if fields[2] in sizes or fields[1] != "app":
                raise ValueError("duplicate or invalid OTA partition")
            sizes[fields[2]] = int(fields[4], 0)
    if set(sizes) != {"ota_0", "ota_1"} or min(sizes.values()) < 1:
        raise ValueError("both OTA app partitions must exist")
    return min(sizes.values())


def load_policy(root):
    """Read expected identity from sources, not duplicated CLI constants."""
    root = Path(root)
    source = (root / "include/Communication/Zigbee/ZigbeeConfig.h").read_text(encoding="utf-8")
    manifest = json.loads((root / "resources/product-version.json").read_text(encoding="utf-8"))
    candidate = manifest.get("ota_file_version")
    if type(candidate) is not int or not 0 < candidate < 0xFFFFFFFF:
        raise ValueError("invalid candidate OTA version")
    app_version = identity(manifest, root)["full"]
    if len(app_version.encode("ascii")) > 31:
        raise ValueError("product version exceeds ESP app descriptor capacity")
    return {
        "manufacturer": _constant(source, "ZIGBEE_OTA_MANUFACTURER_CODE"),
        "image_type": _constant(source, "ZIGBEE_OTA_IMAGE_TYPE"),
        "hardware_version": _constant(source, "ZIGBEE_OTA_UPGRADE_HARDWARE_VERSION"),
        "candidate_version": candidate,
        "app_version": app_version,
        "slot_size": _slot_size((root / "partitions.csv").read_text(encoding="utf-8")),
    }


def inspect_structure(data, policy, previous_version=None):
    """Reject any file that the strict RaCool OTA envelope cannot consume.

    Signature cryptography, physical flash latency and RF delivery are not
    checked here; a structure PASS cannot be used as an OTA release gate.
    """
    if not isinstance(data, bytes):
        raise ValueError("OTA image must be bytes")
    slot = policy["slot_size"]
    if len(data) < HEADER_SIZE + OTA_TAG.size + 1 or len(data) > slot + HEADER_SIZE + OTA_TAG.size:
        raise ValueError("OTA file outside allowed partition envelope")
    (magic, version, header_len, flags, manufacturer, image_type,
     file_version, stack, _name, total) = OTA_HEADER.unpack_from(data)
    if (magic, version, header_len, flags, stack) != (
            FILE_MAGIC, HEADER_VERSION, HEADER_SIZE, HARDWARE_FIELD, STACK_VERSION):
        raise ValueError("unsupported OTA header or optional fields")
    if (manufacturer, image_type) != (policy["manufacturer"], policy["image_type"]):
        raise ValueError("foreign or legacy OTA identity")
    if file_version != policy["candidate_version"]:
        raise ValueError("OTA file version differs from this source candidate")
    if previous_version is not None:
        if type(previous_version) is not int or not 0 <= previous_version < 0xFFFFFFFF:
            raise ValueError("invalid previous OTA version")
        if file_version <= previous_version:
            raise ValueError("OTA candidate is not newer than the previous version")
    if total != len(data):
        raise ValueError("OTA total length mismatch")
    low_hw, high_hw = struct.unpack_from("<HH", data, OTA_HEADER.size)
    if low_hw > high_hw or not low_hw <= policy["hardware_version"] <= high_hw:
        raise ValueError("OTA hardware range mismatch")
    tag_id, payload_size = OTA_TAG.unpack_from(data, header_len)
    app_offset = header_len + OTA_TAG.size
    if tag_id != 0 or payload_size == 0 or payload_size > slot:
        raise ValueError("unsupported or oversized OTA image element")
    if app_offset + payload_size != total:
        raise ValueError("OTA element length mismatch or extra tags")
    app = data[app_offset:]
    if len(app) < 80 or app[0] != APP_MAGIC or app[32:36] != APP_DESCRIPTOR_MAGIC:
        raise ValueError("missing ESP app magic or descriptor")
    try:
        app_version = app[48:80].split(b"\0", 1)[0].decode("ascii")
        parse_version(app_version)
    except (UnicodeDecodeError, ValueError) as error:
        raise ValueError("invalid ESP app descriptor version") from error
    if app_version != policy["app_version"]:
        raise ValueError("ESP app descriptor does not match source version")
    return {
        "status": "STRUCTURE_PASS_SIGNATURE_UNVERIFIED",
        "installation_authorized": False,
        "signature_verified": False,
        "source_version_matched": True,
        "upgrade_order_verified": previous_version is not None,
        "manufacturerCode": manufacturer,
        "imageType": image_type,
        "fileVersion": file_version,
        "fileSize": total,
        "hardwareMin": low_hw,
        "hardwareMax": high_hw,
        "appVersion": app_version,
        "sha512": hashlib.sha512(data).hexdigest(),
    }


def inspect_file(path, root, previous_version=None):
    """Bound bytes read from one file descriptor; reject detected modifications."""
    policy = load_policy(root)
    limit = policy["slot_size"] + HEADER_SIZE + OTA_TAG.size
    with Path(path).open("rb") as source:
        before = os.fstat(source.fileno())
        if not stat.S_ISREG(before.st_mode):
            raise ValueError("OTA input must be a regular file")
        if before.st_size < HEADER_SIZE + OTA_TAG.size + 1 or before.st_size > limit:
            raise ValueError("OTA file outside allowed partition envelope")
        # A concurrent append cannot make this read allocate beyond limit+1.
        image = source.read(limit + 1)
        if len(image) > limit:
            raise ValueError("OTA input grew beyond partition envelope")
        result = inspect_structure(image, policy, previous_version)
        after = os.fstat(source.fileno())
        fingerprint = lambda s: (s.st_dev, s.st_ino, s.st_size,
                                 s.st_mtime_ns, s.st_ctime_ns)
        if fingerprint(before) != fingerprint(after):
            raise ValueError("OTA input changed during inspection")
        # This read-only tool does not lock the file or authorize publication.
        return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", help="Existing Zigbee OTA container, read-only")
    parser.add_argument("--repo-root", type=Path,
                        default=Path(__file__).resolve().parents[2])
    parser.add_argument("--previous-ota-version", type=lambda text: int(text, 0),
                        help="Optional verified previous OTA counter, never guessed")
    args = parser.parse_args(argv)
    try:
        print(json.dumps(inspect_file(args.image, args.repo_root,
                                      args.previous_ota_version), sort_keys=True))
        return 0
    except (OSError, ValueError, KeyError, struct.error) as error:
        # Do not leak absolute paths or input contents to logs.
        label = str(error) if isinstance(error, ValueError) else "input unavailable or malformed"
        print("REJECTED: " + label, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
