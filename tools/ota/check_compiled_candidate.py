#!/usr/bin/env python3
"""Read-only preflight for a built *unsigned* RaCool ESP32-C6 application.

This verifies source/image identity and the existing OTA app slot bound.
It does NOT verify signatures, authorize boot, publish OTA, or use hardware.
"""
import argparse
import hashlib
import json
import os
import stat
import sys
from pathlib import Path

from inspect_local_image import load_policy, parse_version, APP_MAGIC, APP_DESCRIPTOR_MAGIC


def check_compiled_candidate(image_path, root):
    policy = load_policy(root)
    limit = policy["slot_size"]
    with Path(image_path).open("rb") as source:
        before = os.fstat(source.fileno())
        if not stat.S_ISREG(before.st_mode):
            raise ValueError("firmware input is not a regular file")
        if before.st_size < 80 or before.st_size > limit:
            raise ValueError("compiled firmware outside OTA app slot capacity")
        first = source.read(80)
        if len(first) < 80 or first[0] != APP_MAGIC or first[32:36] != APP_DESCRIPTOR_MAGIC:
            raise ValueError("missing ESP image or app descriptor magic")
        try:
            version = first[48:80].split(b"\0", 1)[0].decode("ascii")
            parse_version(version)
        except (UnicodeDecodeError, ValueError) as error:
            raise ValueError("invalid compiled ESP app version") from error
        if version != policy["app_version"]:
            raise ValueError("compiled ESP app version differs from source")
        digest = hashlib.sha256(first)
        size = len(first)
        while True:
            chunk = source.read(min(64 * 1024, limit + 1 - size))
            if not chunk:
                break
            size += len(chunk)
            if size > limit:
                raise ValueError("compiled firmware grew beyond OTA app slot")
            digest.update(chunk)
        after = os.fstat(source.fileno())
        signature = lambda s: (s.st_dev, s.st_ino, s.st_size,
                               s.st_mtime_ns, s.st_ctime_ns)
        if signature(before) != signature(after) or size != before.st_size:
            raise ValueError("compiled firmware changed during inspection")
    return {
        "status": "COMPILED_APP_SOURCE_MATCH_SIGNATURE_UNVERIFIED",
        "installation_authorized": False,
        "signature_verified": False,
        "source_version_matched": True,
        "appVersion": version,
        "otaFileVersion": policy["candidate_version"],
        "fileSize": size,
        "otaSlotCapacity": limit,
        "sha256": digest.hexdigest(),
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--repo-root", type=Path,
                        default=Path(__file__).resolve().parents[2])
    args = parser.parse_args(argv)
    try:
        print(json.dumps(check_compiled_candidate(args.firmware, args.repo_root),
                         sort_keys=True))
        return 0
    except (OSError, ValueError, KeyError) as error:
        message = str(error) if isinstance(error, ValueError) else "input unavailable"
        print("REJECTED: " + message, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
