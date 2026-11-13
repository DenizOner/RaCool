#!/usr/bin/env python3
"""Derive a product identity without confusing SemVer with Zigbee OTA codes."""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

SEMVER = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$")


def parse_version(value):
    match = SEMVER.fullmatch(value)
    if not match:
        raise ValueError("invalid SemVer")
    prerelease = match.group(4)
    if prerelease and any(len(x) > 1 and x[0] == "0" and x.isdigit() for x in prerelease.split(".")):
        raise ValueError("numeric prerelease identifiers cannot have leading zeroes")
    return tuple(int(match.group(i)) for i in range(1, 4)), prerelease, match.group(5)


def next_version(previous, changes):
    base, prerelease, metadata = parse_version(previous)
    if prerelease or metadata or not changes:
        raise ValueError("a final previous release and classified changes are required")
    levels = {"fix": 0, "feat": 1, "breaking": 2}
    if any(change not in levels for change in changes):
        raise ValueError("unclassified change")
    major, minor, patch = base
    kind = max(levels[change] for change in changes)
    return f"{major + 1}.0.0" if kind == 2 else f"{major}.{minor + 1}.0" if kind == 1 else f"{major}.{minor}.{patch + 1}"


def compare_versions(left, right):
    left_core, left_pre, _ = parse_version(left)
    right_core, right_pre, _ = parse_version(right)
    if left_core != right_core:
        return (left_core > right_core) - (left_core < right_core)
    if left_pre is None or right_pre is None:
        return (left_pre is None) - (right_pre is None)
    a, b = left_pre.split("."), right_pre.split(".")
    for x, y in zip(a, b):
        if x == y:
            continue
        if x.isdigit() and y.isdigit():
            return (int(x) > int(y)) - (int(x) < int(y))
        if x.isdigit() != y.isdigit():
            return -1 if x.isdigit() else 1
        return (x > y) - (x < y)
    return (len(a) > len(b)) - (len(a) < len(b))


def check_release(version, previous_version, ota_code, prior_code, digest, prior_digest, channel):
    _, prerelease, _ = parse_version(version)
    if type(ota_code) is not int or type(prior_code) is not int or not 0 <= prior_code <= 0xffffffff or not 0 <= ota_code <= 0xffffffff:
        raise ValueError("invalid OTA code")
    if not re.fullmatch(r"[0-9a-f]{64}", digest) or not re.fullmatch(r"[0-9a-f]{64}", prior_digest):
        raise ValueError("invalid image digest")
    ordering = compare_versions(version, previous_version)
    if ordering < 0 or (ordering == 0 and digest != prior_digest):
        raise ValueError("product version regressed or was reused for another image")
    if channel not in ("stable", "prerelease"):
        raise ValueError("unknown release channel")
    if channel == "stable" and prerelease:
        raise ValueError("prerelease forbidden on stable channel")
    if ota_code < prior_code or (ota_code == prior_code and digest != prior_digest):
        raise ValueError("OTA code is not monotonic or was reused for another image")


def plan_release(history, changes, ota_code):
    if not isinstance(history, list) or not history:
        raise ValueError("an explicit release history is required")
    if not isinstance(changes, list) or not changes:
        raise ValueError("classified release notes are required")
    for index, entry in enumerate(history):
        if not isinstance(entry, dict) or set(entry) != {"version", "ota_code", "sha256"}:
            raise ValueError("invalid release history entry")
        _, prerelease, metadata = parse_version(entry["version"])
        if prerelease or metadata or type(entry["ota_code"]) is not int or not 0 <= entry["ota_code"] <= 0xffffffff:
            raise ValueError("history must contain final releases and valid OTA codes")
        if not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]):
            raise ValueError("invalid historical image digest")
        if index:
            previous = history[index - 1]
            if compare_versions(entry["version"], previous["version"]) <= 0 or entry["ota_code"] <= previous["ota_code"]:
                raise ValueError("release history is not strictly increasing")
    for item in changes:
        if (not isinstance(item, dict) or set(item) != {"type", "summary", "commit"} or
                item["type"] not in ("fix", "feat", "breaking") or
                not isinstance(item["summary"], str) or not item["summary"].strip() or
                not isinstance(item["commit"], str) or not re.fullmatch(r"[0-9a-f]{40,64}", item["commit"])):
            raise ValueError("each change needs a classification, summary and full commit ID")
    previous = history[-1]
    candidate = next_version(previous["version"], [item["type"] for item in changes])
    if type(ota_code) is not int or not previous["ota_code"] < ota_code <= 0xffffffff:
        raise ValueError("a new release needs a new OTA code")
    return {"version": candidate, "ota_code": ota_code, "changes": changes}


def check_commit_coverage(root, previous_version, changes):
    tag = "v" + previous_version
    def run(*args):
        return subprocess.run(["git", *args], cwd=root, text=True, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, check=False)
    ancestor = run("merge-base", "--is-ancestor", tag, "HEAD")
    if ancestor.returncode:
        raise ValueError("previous release tag is missing or is not an ancestor")
    revisions = run("rev-list", tag + "..HEAD")
    if revisions.returncode:
        raise ValueError("cannot inspect commits since the previous release")
    actual = revisions.stdout.splitlines()
    declared = [item["commit"] for item in changes]
    if not actual or len(declared) != len(set(declared)) or set(actual) != set(declared):
        raise ValueError("every commit since the last release must have exactly one classification")


def inspect_image(path):
    image = Path(path).read_bytes()
    if len(image) < 80 or image[32:36] != bytes.fromhex("3254cdab"):
        raise ValueError("image has no ESP-IDF app descriptor at the expected offset")
    raw = image[48:80].split(b"\0", 1)[0]
    try:
        product_version = raw.decode("ascii")
        parse_version(product_version)
    except (UnicodeDecodeError, ValueError) as error:
        raise ValueError("image app descriptor has an invalid product version") from error
    return product_version, hashlib.sha256(image).hexdigest()


def git_identity(root):
    # A source archive nested inside an unrelated repository is still an archive.
    if not (Path(root) / ".git").exists():
        return None
    def run(*args):
        return subprocess.run(["git", "-c", "core.safecrlf=false", *args], cwd=root, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    revision = run("rev-parse", "--verify", "HEAD")
    if revision.returncode:
        raise ValueError("cannot read Git revision")
    revision_id = revision.stdout.strip()
    if not re.fullmatch(r"[0-9a-f]{40,64}", revision_id):
        raise ValueError("invalid Git revision")
    tracked = run("diff-index", "--quiet", "HEAD", "--")
    untracked = run("ls-files", "--others", "--exclude-standard")
    if tracked.returncode not in (0, 1) or untracked.returncode:
        raise ValueError("cannot determine Git worktree state")
    tags = run("tag", "--points-at", "HEAD")
    if tags.returncode:
        raise ValueError("cannot determine release tags")
    return revision_id[:12], tracked.returncode == 1 or bool(untracked.stdout.strip()), tags.stdout.splitlines()


def identity(manifest, root):
    base = manifest["base_version"]
    _, prerelease, metadata = parse_version(base)
    if prerelease or metadata:
        raise ValueError("manifest base must be a final SemVer core")
    ota = manifest["ota_file_version"]
    if type(ota) is not int or not 0 <= ota <= 0xffffffff:
        raise ValueError("invalid manifest OTA code")
    git = git_identity(root)
    if git:
        revision, dirty, tags = git
        if not dirty and "v" + base in tags:
            return {"full": base, "short": "v" + base, "ota_file_version": ota}
        suffix = "g" + revision + (".dirty" if dirty else "")
    else:
        suffix = manifest.get("archive_identity", "")
        if not re.fullmatch(r"[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*", suffix):
            raise ValueError("source archives require an explicit identity")
    return {"full": base + "-dev.0+" + suffix, "short": "v" + base + "*", "ota_file_version": ota}


def write_changed(path, content):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding="utf-8") != content:
        path.write_text(content, encoding="utf-8", newline="\n")


def zigbee_version(full):
    parse_version(full)
    # ZCL Basic SWBuildID holds at most 16 bytes. Metadata is available in CLI.
    value = full.split("+", 1)[0]
    if len(value.encode("ascii")) > 16:
        raise ValueError("product version exceeds the Zigbee SWBuildID limit")
    return value


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    generate = commands.add_parser("generate")
    generate.add_argument("--manifest", required=True)
    generate.add_argument("--root", required=True)
    generate.add_argument("--output", required=True)
    candidate = commands.add_parser("next")
    candidate.add_argument("--previous", required=True)
    candidate.add_argument("--change", action="append", required=True)
    check = commands.add_parser("check-release")
    check.add_argument("--version", required=True)
    check.add_argument("--previous-version", required=True)
    check.add_argument("--ota-code", type=int, required=True)
    check.add_argument("--previous-ota-code", type=int, required=True)
    check.add_argument("--image", required=True)
    check.add_argument("--previous-image-sha256", required=True)
    check.add_argument("--channel", required=True)
    check.add_argument("--root", required=True)
    plan = commands.add_parser("plan-release")
    plan.add_argument("--history", required=True)
    plan.add_argument("--changes", required=True)
    plan.add_argument("--ota-code", type=int, required=True)
    plan.add_argument("--root", required=True)
    args = parser.parse_args()
    if args.command == "next":
        print(next_version(args.previous, args.change))
        return
    if args.command == "check-release":
        git = git_identity(args.root)
        if git is None or git[1]:
            raise ValueError("release validation requires a clean Git worktree")
        if args.channel == "stable" and "v" + args.version not in git[2]:
            raise ValueError("stable release validation requires its matching tag on HEAD")
        manifest = json.loads((Path(args.root) / "resources/product-version.json").read_text(encoding="utf-8"))
        product = identity(manifest, args.root)
        if product["full"] != args.version:
            raise ValueError("requested version differs from the generated product identity")
        if product["ota_file_version"] != args.ota_code:
            raise ValueError("OTA code differs from the product manifest")
        image_version, image_digest = inspect_image(args.image)
        if image_version != args.version:
            raise ValueError("image app descriptor does not match the requested version")
        check_release(args.version, args.previous_version, args.ota_code, args.previous_ota_code,
                      image_digest, args.previous_image_sha256, args.channel)
        print("release validation passed")
        return
    if args.command == "plan-release":
        git = git_identity(args.root)
        if git is None or git[1]:
            raise ValueError("release planning requires a clean Git worktree")
        history = json.loads(Path(args.history).read_text(encoding="utf-8"))
        changes = json.loads(Path(args.changes).read_text(encoding="utf-8"))
        proposal = plan_release(history, changes, args.ota_code)
        check_commit_coverage(args.root, history[-1]["version"], changes)
        print(json.dumps(proposal, indent=2))
        return
    manifest = json.loads(Path(args.manifest).read_text(encoding="utf-8"))
    version = identity(manifest, args.root)
    if len(version["full"].encode("ascii")) > 31:
        raise ValueError("product version exceeds the ESP-IDF app descriptor limit")
    zcl_version = zigbee_version(version["full"])
    header = ("#ifndef RACOOL_PRODUCT_VERSION_GENERATED_H\n#define RACOOL_PRODUCT_VERSION_GENERATED_H\n"
              f'#define RACOOL_PRODUCT_VERSION "{version["full"]}"\n'
              f'#define RACOOL_PRODUCT_VERSION_SHORT "{version["short"]}"\n'
              f'#define RACOOL_PRODUCT_ZCL_VERSION "\\x{len(zcl_version):02x}" "{zcl_version}"\n'
              f'#define RACOOL_OTA_FILE_VERSION {version["ota_file_version"]}u\n#endif\n')
    write_changed(Path(args.output) / "ProductVersion.generated.h", header)
    write_changed(Path(args.output) / "ProductVersion.generated.cmake", f'set(PROJECT_VER "{version["full"]}")\n')


if __name__ == "__main__":
    main()
