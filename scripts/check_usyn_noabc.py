#!/usr/bin/env python3
"""Build/run the real USYN CLI after removing ABC in a disposable source copy.

Includes current tracked and untracked source edits. Never mutates the checkout.
The retained fixture contains source, dependency audit and Bazel test logs.
This is an explicit build gate, not a nested Bazel test in the default suite.
"""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, help="new directory to retain the build fixture")
    parser.add_argument("--mode", choices=("opt", "dbg"), default="opt")
    parser.add_argument("--bazel", default="bazel")
    args = parser.parse_args()
    source = Path(__file__).resolve().parents[1]
    fixture = args.fixture.resolve() if args.fixture else Path(tempfile.mkdtemp(prefix="livehd-noabc-"))
    if fixture == source or source in fixture.parents:
        raise ValueError("the disposable fixture must be outside the source checkout")
    if args.fixture:
        fixture.mkdir(parents=True, exist_ok=False)
    print(f"ABC-removed fixture: {fixture}", flush=True)
    paths = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=source
    ).decode().split("\0")
    for name in dict.fromkeys(paths):
        if not name or name.startswith(("pass/abc/", "repros/")) or name in (
            "packages/abc.BUILD", "packages/abc.patch", "MODULE.bazel.lock"
        ):
            continue
        src = source / name
        if not src.is_file():
            continue
        dst = fixture / name
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    module = fixture / "MODULE.bazel"
    text, count = re.subn(
        r'(?m)^new_git_repository\(\s*name = "abc",.*?^\)\n',
        "# ABC repository deliberately removed by check_usyn_noabc.py\n",
        module.read_text(), flags=re.DOTALL,
    )
    if count != 1:
        raise RuntimeError("expected exactly one ABC repository declaration; update the removal audit")
    module.write_text(text)
    assert not (fixture / "pass/abc").exists()
    assert not (fixture / "packages/abc.BUILD").exists()
    bazel = [args.bazel]
    config = ["-c", args.mode, "--define=livehd_abc=false"]
    audit = subprocess.run(
        bazel + ["cquery"] + config + [
            'filter("[+@]abc//|//pass/abc", deps(//lhd))', "--output=label"
        ], cwd=fixture, text=True, stdout=subprocess.PIPE, check=True,
    )
    (fixture / "noabc-dependencies.txt").write_text(audit.stdout)
    if audit.stdout.strip():
        raise RuntimeError("ABC remains in the configured lhd dependency closure")
    subprocess.run(
        bazel + ["test"] + config + [
            "--local_test_jobs=1", "--test_output=errors", "//lhd/tests:lhd_usyn_noabc_smoke"
        ], cwd=fixture, check=True,
    )
    # The test contains a source->logical-netlist run with no Liberty, state/name
    # checks, independent CVC5 proof/refutation, and unavailable-tmap diagnostics.
    print(f"PASS: ABC-removed {args.mode} CLI build/run; evidence retained in {fixture}", flush=True)


if __name__ == "__main__":
    main()
