#!/usr/bin/env python3
"""Prepare the pinned whole CORE-ET icache_top input with explicit imports.

Usage:
  python3 pass/lean/scripts/coreet_icache_input.py \
      --source-root generated/core_et_c72b6a2 --output-dir generated/icache_input

The source root must contain the complete hw/ snapshot of CORE-ET commit
c72b6a2fe4aaad5a9eef437776864ff71f4b55d7, without .git or generated files.
The adjacent profile authenticates every file and the original 31-source order.
The output must be new. No default sibling repository or native tool is used.

Use the returned filelist with lhd compile's yosys.filelist_file setting and
include_dir=<output>/rtl_snapshot/hw/include. The empty icache_anchor.sv supplies
an anchor; icache_top remains the whole top. This prepares a qualified benchmark
configuration. It neither normalizes memories nor emits/adopts a certificate.
"""

import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys


PROFILE = Path(__file__).with_name("coreet_icache_profile.json")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(root):
    result = []
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"symlink in source snapshot: {path}")
        if path.is_file():
            result.append({"path": path.relative_to(root).as_posix(),
                           "sha256": sha256(path), "bytes": path.stat().st_size})
        elif not path.is_dir():
            raise ValueError(f"nonregular snapshot entry: {path}")
    return result


def digest(entries):
    pairs = [[entry["path"], entry["sha256"]] for entry in entries]
    return hashlib.sha256(json.dumps(pairs, separators=(",", ":")).encode()).hexdigest()


def checked_bindings(root, profile):
    declarations = {}
    for name, binding in profile["bindings"].items():
        found = []
        pattern = re.compile(r"(?m)^\s*(?:localparam|parameter)\s+([^;\n]*?)\b"
                             + re.escape(name) + r"\s*=\s*([^;\n]+);")
        for relative in profile["ordered_sources"]:
            for match in pattern.finditer((root / relative).read_text()):
                actual = (match.group(1).strip(), match.group(2).strip())
                expected = (binding["type"], binding["rhs"])
                if actual != expected:
                    raise ValueError(f"changed or unequal {name} in {relative}: "
                                     f"{actual!r}, expected {expected!r}")
                found.append({"path": relative, "type": actual[0], "rhs": actual[1]})
        if [entry["path"] for entry in found] != binding["declaration_paths"]:
            raise ValueError(f"unexpected declaration locations for {name}")
        declarations[name] = found
    return declarations


def qualify_imports(source, names):
    pattern = re.compile(r"(?m)^([ \t]*)import minion_pkg::\*;")

    def replacement(match):
        return match.group(0) + "".join(
            "\n" + match.group(1) + "import minion_pkg::" + name + ";"
            for name in names)

    changed, count = pattern.subn(replacement, source)
    if count != 1:
        raise ValueError(f"expected one minion_pkg wildcard scope, found {count}")
    return changed


def prepare(source_root, output):
    profile = json.loads(PROFILE.read_text())
    if profile["schema"] != 1:
        raise ValueError("unsupported adapter profile")
    source_root = source_root.resolve(strict=True)
    if not source_root.is_dir():
        raise ValueError("source root must be a directory")
    output = output.absolute()
    if output.exists() or output.is_symlink():
        raise ValueError(f"output already exists: {output}")
    if not output.parent.is_dir():
        raise ValueError("output parent must already exist")
    output = output.parent.resolve(strict=True) / output.name
    if output == source_root or source_root in output.parents:
        raise ValueError("output must be outside the source snapshot")

    original = inventory(source_root)
    # Check the duplicate declarations before publication so a configuration
    # change receives a specific diagnosis, then authenticate the full closure.
    declarations = checked_bindings(source_root, profile)
    if len(original) != profile["snapshot_file_count"]:
        raise ValueError("complete source snapshot file count differs from the profile")
    if digest(original) != profile["snapshot_digest_sha256"]:
        raise ValueError("complete source snapshot digest differs from the profile")

    output.mkdir()
    incomplete = output / "INCOMPLETE"
    incomplete.write_text("Adapter construction has not completed. Do not use this input.\n")
    snapshot = output / "rtl_snapshot"
    shutil.copytree(source_root, snapshot, copy_function=shutil.copy2)
    if inventory(snapshot) != original:
        raise ValueError("copied snapshot differs from the authenticated source")
    for entry in original:
        original_path = source_root / entry["path"]
        copied_path = snapshot / entry["path"]
        src_stat, dst_stat = original_path.stat(), copied_path.stat()
        if (src_stat.st_dev, src_stat.st_ino) == (dst_stat.st_dev, dst_stat.st_ino):
            raise ValueError("snapshot copy shares a source inode")
        if dst_stat.st_nlink != 1:
            raise ValueError("snapshot copy has a shared inode")

    for change in profile["changes"]:
        path = snapshot / change["path"]
        if sha256(path) != change["original_sha256"]:
            raise ValueError(f"source scope changed: {change['path']}")
        # Preserve all original bytes except the inserted import statements.
        data = path.read_bytes()
        adapted = qualify_imports(data.decode("utf-8"), change["explicit_bindings"])
        path.write_bytes(adapted.encode("utf-8"))
        if sha256(path) != change["adapted_sha256"]:
            raise ValueError(f"adapted scope differs from the validated profile: {change['path']}")

    adapted = inventory(snapshot)
    expected = {entry["path"]: entry["sha256"] for entry in original}
    expected.update({change["path"]: change["adapted_sha256"] for change in profile["changes"]})
    if {entry["path"]: entry["sha256"] for entry in adapted} != expected:
        raise ValueError("unrequested snapshot content changed")
    if inventory(source_root) != original:
        raise ValueError("source snapshot changed during adapter construction")

    filelist = output / "icache_top_explicit_imports.f"
    filelist.write_text("// Explicit-import CORE-ET icache_top input; original source order.\n"
                        + "\n".join(str(snapshot / relative)
                                    for relative in profile["ordered_sources"]) + "\n")
    anchor = output / "icache_anchor.sv"
    anchor.write_text("// Empty anchor. Real CORE-ET sources arrive via yosys.filelist_file.\n")
    manifest = {
        "schema": 1, "status": "PREPARED_INPUT",
        "at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "profile_source_pin": profile["source_pin"],
        "profile_sha256": sha256(PROFILE), "adapter_sha256": sha256(Path(__file__)),
        "source_root": str(source_root), "source_snapshot_digest_sha256": digest(original),
        "snapshot_file_count": len(original),
        "ordered_source_count": len(profile["ordered_sources"]),
        "filelist": str(filelist), "filelist_sha256": sha256(filelist),
        "anchor": str(anchor), "anchor_sha256": sha256(anchor),
        "include_dir": str(snapshot / "hw/include"),
        "declarations": declarations, "changes": profile["changes"],
        "original_inventory": original, "adapted_inventory": adapted,
        "scope": profile["scope"],
    }
    (output / "adapter_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    incomplete.unlink()
    return {key: manifest[key] for key in ["status", "filelist", "anchor", "include_dir",
                                         "snapshot_file_count", "ordered_source_count", "scope"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = prepare(args.source_root, args.output_dir)
    except (ValueError, OSError, KeyError, UnicodeError) as error:
        print(f"coreet_icache_input: {error}", file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
