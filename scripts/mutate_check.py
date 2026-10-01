#!/usr/bin/env python3
"""Mutation-check a test: each mutant must be killed, and the source must come
back whatever happens.

A guard is only a guard if removing it breaks a NAMED test.  Doing that by hand
means editing a source file, running the test, and editing it back -- and the
edit-back is the part that goes wrong: an exception, a Ctrl-C or a bazel crash
between the two leaves a mutated file in the tree, which then gets committed.

So the restore is in a `finally` AND on SIGINT/SIGTERM, the original bytes are
copied to a named project-local directory first (never /tmp: this project keeps
run artifacts under a `generated/` path, which also survives a machine
rebuild), and the file is restored from that copy rather than re-edited.

Spec (JSON):
  {"target":  "//pass/lean:cert_dag_order_test",
   "file":    "pass/lean/lean_common.cpp",
   "backup":  "<dir>",            # optional; generated/mutate_check/runtime_tmp
   "rebuild": ["//lhd:lhd"],      # optional; rebuilt after every restore
   "bazel":   ["--repo_env=CC=/usr/bin/gcc-15", ...],
   "mutants": [{"name": "...", "old": "...", "new": "..."}, ...]}

`old` must occur EXACTLY ONCE in the file: a pattern that matches twice (or
none) mutates something other than what its name claims, so it is refused.

Exit 0 only when every mutant was killed by at least one named test.  A mutant
that fails to BUILD is not a kill -- it proves nothing about the test -- and is
reported as such.
"""
import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys

# Two shapes of "a named case failed": gtest's `[  FAILED  ] Suite.Case` and a
# shell test's own `FAIL: <what>` line.  Matching only the first silently
# reported every sh_test mutant as an unnamed kill.
GTEST_RE = re.compile(r"^\[  FAILED  \] (\S+?)(?:,.*)?$", re.M)
SHELL_RE = re.compile(r"^FAIL: (.+)$", re.M)


def failing_names(out):
    names = sorted({m for m in GTEST_RE.findall(out) if "." in m})
    if names:
        return names
    # Keep the shell test's OWN `FAIL: <what>` lines and drop the noise around
    # them: bazel's summary uses the same prefix ("FAIL: //pkg:target (Exit 1)
    # (see .../test.log)"), and a log path is not a finding. A shell test's
    # trailing "FAIL: <test name>" summary is dropped the same way.
    sh = []
    for m in SHELL_RE.findall(out):
        m = m.strip()
        if m.startswith("//") or " " not in m:
            continue
        sh.append(m)
    return sorted(set(sh))


def run_test(target, bazel_args):
    """-> (status, failing test names).  status is 'pass' | 'fail' | 'build'."""
    cmd = (["bazel", "test", target, "--nocache_test_results", "--test_output=errors"]
           + list(bazel_args))
    p = subprocess.run(cmd, capture_output=True, text=True)
    out = p.stdout + p.stderr
    if p.returncode == 0:
        return "pass", [], out
    names = failing_names(out)
    if p.returncode == 1 and not names:
        # bazel: 1 = build error, 3 = tests failed. A build error kills nothing.
        return "build", [], out
    return "fail", names, out


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("spec")
    args = ap.parse_args()
    spec = json.load(open(args.spec))
    # What to rebuild after each mutant, so bazel-bin never keeps mutated
    # artifacts.  Default to the binary every by-hand probe in this project
    # runs straight out of bazel-bin.
    spec.setdefault("rebuild", ["//lhd:lhd"])

    path = spec["file"]
    # Project-local by default. A committed spec must not name one machine's
    # scratch directory, and /tmp is refused outright below: this project keeps
    # run artifacts under generated/, which also survives a machine rebuild.
    backup_dir = spec.get("backup", "generated/mutate_check/runtime_tmp")
    if backup_dir.startswith("/tmp/") or backup_dir == "/tmp":
        print(f"FAIL: backup directory {backup_dir!r} is under /tmp; use a project-local "
              f"generated/ path so the copy survives and is findable")
        return 2
    os.makedirs(backup_dir, exist_ok=True)
    backup = os.path.join(backup_dir, os.path.basename(path) + ".orig")
    shutil.copy2(path, backup)

    restored = [False]

    def restore(*_):
        if not restored[0]:
            shutil.copy2(backup, path)
            restored[0] = True
            # REBUILD, not just restore.  bazel-bin keeps whatever the last
            # mutant produced, and anything run afterwards from bazel-bin --
            # a sweep, a census, a by-hand probe -- would silently be using a
            # DELIBERATELY BROKEN binary.  That happened: a four-module CORE-ET
            # census ran against a mutant and its refusals had to be thrown
            # away.  Rebuilding here is cheap and makes the tree honest again.
            try:
                subprocess.run(["bazel", "build"] + list(spec.get("rebuild", [])) + list(spec.get("bazel", [])),
                               capture_output=True, text=True, timeout=3600)
            except Exception:
                pass

    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, lambda s, f: (restore(), sys.exit(130)))

    bad = 0
    try:
        status, names, out = run_test(spec["target"], spec.get("bazel", []))
        if status != "pass":
            print(f"FAIL: the baseline does not pass ({status}) -- mutation says nothing yet")
            print("\n".join(out.splitlines()[-15:]))
            return 1
        print(f"baseline: {spec['target']} PASSES")

        src = open(backup).read()
        for m in spec["mutants"]:
            n = src.count(m["old"])
            if n != 1:
                print(f"{m['name']:<30} SPEC-ERROR: its pattern occurs {n} times, not once")
                bad += 1
                continue
            open(path, "w").write(src.replace(m["old"], m["new"], 1))
            status, names, out = run_test(spec["target"], spec.get("bazel", []))
            shutil.copy2(backup, path)
            if status == "fail" and names:
                print(f"{m['name']:<30} KILLED by {' '.join(names)}")
            elif status == "fail":
                print(f"{m['name']:<30} FAILED THE TEST BUT NAMED NOTHING "
                      f"(a timeout or a crash is a weak kill; make it return a wrong ANSWER)")
                bad += 1
            elif status == "build":
                print(f"{m['name']:<30} DID NOT BUILD -- proves nothing; rewrite it to compile")
                bad += 1
            else:
                print(f"{m['name']:<30} SURVIVED -- no test covers this guard")
                bad += 1
    finally:
        restore()
        # Prove it: the file must be byte-identical to the copy taken up front.
        if open(path, "rb").read() != open(backup, "rb").read():
            print(f"FAIL: {path} was NOT restored; the original is at {backup}")
            return 2

    print(f"restored {path} (verified byte-identical to {backup})")
    if bad:
        print(f"FAIL: {bad} mutant(s) were not cleanly killed")
        return 1
    print("PASS: every mutant was killed by a named test")
    return 0


if __name__ == "__main__":
    sys.exit(main())
