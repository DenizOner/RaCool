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
import subprocess
import sys
from pathlib import Path

from inspect_local_image import load_policy, parse_version, APP_MAGIC, APP_DESCRIPTOR_MAGIC
from product_version import git_identity


def _source_head(root):
    result = subprocess.run(["git", "rev-parse", "--verify", "HEAD"],
                            cwd=root, capture_output=True, text=True, check=False)
    head = result.stdout.strip()
    if result.returncode != 0 or len(head) not in (40, 64) or any(
            ch not in "0123456789abcdef" for ch in head):
        raise ValueError("cannot verify source checkout HEAD")
    return head


def capture_source_identity(destination, root):
    """Snapshot the source identity once BEFORE the compiler mutates build state."""
    root = Path(root)
    git = git_identity(root)
    if git is None or git[1]:
        raise ValueError("source identity capture requires a clean Git checkout")
    policy = load_policy(root)
    output = Path(destination)
    # Capture is strictly build-local; never write tracked source or user files.
    build_root = (root / ".pio").resolve()
    if not output.resolve().is_relative_to(build_root):
        raise ValueError("source identity snapshot must live inside .pio")
    snapshot = {
        "head": _source_head(root),
        "manifest_sha256": hashlib.sha256(
            (root / "resources/product-version.json").read_bytes()).hexdigest(),
        "app_version": policy["app_version"],
        "candidate_version": policy["candidate_version"],
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    try:
        temporary.write_text(json.dumps(snapshot, sort_keys=True) + "\n", encoding="ascii")
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    return snapshot


def _read_source_identity(path, root, policy):
    root = Path(root)
    source = Path(path)
    with source.open("rb") as stream:
        blob = stream.read(2049)
    if len(blob) > 2048:
        raise ValueError("source snapshot exceeds limit")
    try:
        payload = json.loads(blob)
    except (UnicodeDecodeError, ValueError) as error:
        raise ValueError("malformed source identity snapshot") from error
    if type(payload) is not dict or set(payload) != {
            "head", "manifest_sha256", "app_version", "candidate_version"}:
        raise ValueError("invalid source identity snapshot")
    expected_hash = hashlib.sha256(
        (root / "resources/product-version.json").read_bytes()).hexdigest()
    if (payload["head"] != _source_head(root) or
            payload["manifest_sha256"] != expected_hash or
            type(payload["candidate_version"]) is not int or
            payload["candidate_version"] != policy["candidate_version"]):
        raise ValueError("source identity snapshot does not match checkout")
    version = payload["app_version"]
    if not isinstance(version, str):
        raise ValueError("invalid frozen app version")
    parse_version(version)
    return version


def check_compiled_candidate(image_path, root, source_identity=None):
    policy = load_policy(root)
    expected = (_read_source_identity(source_identity, root, policy)
                if source_identity is not None else policy["app_version"])
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
        if version != expected:
            # Version strings are public, non-secret firmware metadata.
            raise ValueError(f"compiled ESP app version differs from source "
                             f"(app={version!r}, expected={expected!r})")
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
    operation = parser.add_mutually_exclusive_group(required=True)
    operation.add_argument("--firmware", type=Path)
    operation.add_argument("--capture-source-identity", type=Path)
    parser.add_argument("--source-identity", type=Path,
                        help="Pre-build identity snapshot (CI only)")
    parser.add_argument("--repo-root", type=Path,
                        default=Path(__file__).resolve().parents[2])
    args = parser.parse_args(argv)
    try:
        if args.capture_source_identity is not None:
            if args.source_identity is not None:
                raise ValueError("source snapshot cannot be provided when capturing")
            # The only write is to an ignored build-local identity file.
            capture_source_identity(args.capture_source_identity, args.repo_root)
            print("SOURCE_IDENTITY_CAPTURED")
        else:
            print(json.dumps(check_compiled_candidate(args.firmware, args.repo_root,
                             args.source_identity), sort_keys=True))
        return 0
    except (OSError, ValueError, KeyError) as error:
        message = str(error) if isinstance(error, ValueError) else "input unavailable"
        print("REJECTED: " + message, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
