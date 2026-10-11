#!/usr/bin/env python3
"""Controls for `fp_replay.py`'s admission boundary, on the REAL CLI.

WHY.  Two earlier versions of the admitter were too permissive and I did not
find it; retained controls did.  The second version admitted a row with no
worker timing, no launch marker, ONE stimulus, ONE trace cycle, a command that
named a different binary, and a capture naming a different binary.  So the
boundary is now tested case by case, and each case asserts the REFUSAL REASON,
not merely a nonzero exit -- a check that only looks at the status cannot tell
"refused for the right reason" from "refused for another".

Every case runs on an ISOLATED COPY of a real run's evidence. Nothing in
`generated/` is mutated, and `--admit-only` is used throughout so no benchmark
or build executes.
"""

import json, pathlib, re, shutil, subprocess, sys, tempfile, os

ROOT = pathlib.Path(__file__).resolve().parents[4]
CLI = ROOT / "pass" / "lean" / "scripts" / "fp_replay.py"
SRC = ROOT / "generated" / "master_port" / "fp" / "nat-proto-O2"
fails = []


def ok(n, note=""):
    print(f"PASS  {n}" + (f"  {note}" if note else ""))


def bad(n, note=""):
    print(f"FAIL  {n}" + (f"  {note}" if note else ""))
    fails.append(n)


def stage(tmp, block="pmp"):
    """An isolated copy of one real run plus its plan."""
    d = tmp / f"case_{len(list(tmp.iterdir()))}"
    (d / "runs").mkdir(parents=True)
    for f in (SRC / "runs").glob(f"{block}.*"):
        shutil.copy2(f, d / "runs" / f.name)
    # The binary is used READ-ONLY and is not copied: the recorded command names
    # the real one, and copying it elsewhere would make every case refuse for
    # "the command does not name this binary" -- which is the admitter being
    # right about a fixture that was wrong.
    plan = json.loads((SRC / "plan_canonical.json").read_text())
    row = [r for r in plan["rows"] if r["name"] == block][0]
    plan["outdir"] = str(d / "runs")
    plan["rows"] = [dict(row)]
    plan.pop("capture", None)
    (d / "plan.json").write_text(json.dumps(plan))
    return d


def run(d, extra=()):
    r = subprocess.run([sys.executable, "-I", str(CLI), "--plan", str(d / "plan.json"),
                        "--out", str(d / "join.tsv"), "--admit-only", *extra],
                       capture_output=True, text=True)
    status, refus = "", ""
    lines = [l for l in (d / "join.tsv").read_text().splitlines() if not l.startswith("#")]
    if len(lines) > 1:
        cols, vals = lines[0].split("\t"), lines[1].split("\t")
        rec = dict(zip(cols, vals))
        status, refus = rec.get("status", ""), rec.get("refusals", "")
    return r.returncode, status, refus, (d / "join.tsv")


def case(name, mutate, want_status, want_reason, tmp):
    d = stage(tmp)
    mutate(d)
    rc, st, refus, _ = run(d)
    if st != want_status:
        bad(name, f"status {st!r}, wanted {want_status!r}; refusals={refus[:120]}")
    elif want_reason and want_reason not in refus:
        bad(name, f"refused, but not for {want_reason!r}: {refus[:140]}")
    else:
        ok(name, (refus[:76] if refus else "admitted"))


def main():
    if not (SRC / "runs" / "pmp.log").is_file():
        print("FATAL: no retained run to copy from", file=sys.stderr)
        return 2
    tmp = pathlib.Path(tempfile.mkdtemp(prefix="fpreplayctl_", dir=str(ROOT / "temp")))
    R = lambda d, suf: d / "runs" / f"pmp.{suf}"

    case("positive", lambda d: None, "pass", "", tmp)
    case("missing_worker_time", lambda d: R(d, "time").unlink(), "refused",
         "missing evidence file: .time", tmp)
    case("malformed_worker_time", lambda d: R(d, "time").write_text("garbage\n"),
         "refused", "worker `exit` trailer", tmp)
    case("duplicate_worker_trailer",
         lambda d: R(d, "time").write_text(R(d, "time").read_text() + "exit 0\n"),
         "refused", "uniquely parsed worker", tmp)
    case("missing_status", lambda d: R(d, "exit").unlink(), "refused",
         "missing evidence file: .exit", tmp)
    case("malformed_status", lambda d: R(d, "exit").write_text("zero\n"), "refused",
         "malformed outer status", tmp)
    case("missing_launch_marker", lambda d: R(d, "cmd").unlink(), "refused",
         "missing evidence file: .cmd", tmp)
    case("missing_attempt", lambda d: R(d, "attempt").unlink(), "refused",
         "not scoped to a run", tmp)
    case("foreign_attempt_file",
         lambda d: R(d, "exit.attempt").write_text("some-other-attempt\n"), "refused",
         "not scoped to attempt", tmp)
    case("missing_capture", lambda d: R(d, "capture.json").unlink(), "refused",
         "no pre-launch capture", tmp)
    # The SAME fixture with --allow-missing-capture may be kept as an
    # observation, and must still never be credited.
    d = stage(tmp); R(d, "capture.json").unlink()
    rc, st, refus, _ = run(d, extra=["--allow-missing-capture"])
    if st == "observed_evidence_limited" and "no pre-launch capture" in refus:
        ok("allow_missing_capture_never_passes", "capped at evidence-limited")
    else:
        bad("allow_missing_capture_never_passes", f"status {st!r}")
    case("malformed_capture", lambda d: R(d, "capture.json").write_text("{nope"),
         "refused", "capture does not parse", tmp)
    case("capture_after_launch",
         lambda d: os.utime(R(d, "capture.json"),
                            (R(d, "cmd").stat().st_mtime + 10,) * 2),
         "refused", "NEWER than the launch marker", tmp)

    def wrong_cap_binary(d):
        c = json.loads(R(d, "capture.json").read_text())
        s = json.dumps(c).replace(
            re.search(r"[0-9a-f]{64}", json.dumps(c)).group(0), "f" * 64)
        R(d, "capture.json").write_text(s)
    case("wrong_capture_binary", wrong_cap_binary, "refused",
         "capture binary_sha256 is", tmp)

    def cap_hash_in_notes(d):
        # The expected hashes appear in a NOTE field while the schema fields are
        # wrong. Searching the JSON text accepted this.
        c = json.loads(R(d, "capture.json").read_text())
        good_b, good_c = c["binary_sha256"], c["cert_sha256"]
        c["binary_sha256"] = "a" * 64
        c["cert_sha256"] = "b" * 64
        c["note"] = f"expected {good_b} and {good_c}"
        R(d, "capture.json").write_text(json.dumps(c))
    case("capture_hash_only_in_notes", cap_hash_in_notes, "refused",
         "capture binary_sha256 is", tmp)

    def cap_wrong_evidence(d):
        c = json.loads(R(d, "capture.json").read_text())
        c["evidence"] = "reconstructed"
        R(d, "capture.json").write_text(json.dumps(c))
    case("capture_not_prelaunch_strength", cap_wrong_evidence, "refused",
         "not 'captured-pre-launch'", tmp)

    def wrong_argv(d):
        a = json.loads(R(d, "argv.json").read_text())
        a["runner_argv"] = ["/some/other/binary", "--file-ab", "/x.dcert", "1", "2"]
        R(d, "argv.json").write_text(json.dumps(a))
    case("wrong_launch_command", wrong_argv, "refused", "is not this row's", tmp)

    def fuel_substring(d):
        # 12000000 CONTAINS 200000. A substring test accepted it.
        a = json.loads(R(d, "argv.json").read_text())
        a["runner_argv"] = [x if x != "200000" else "12000000" for x in a["runner_argv"]]
        R(d, "argv.json").write_text(json.dumps(a))
        c = R(d, "cmd").read_text().replace("200000", "12000000")
        R(d, "cmd").write_text(c)
    case("fuel_substring_rejected", fuel_substring, "refused", "is not this row's", tmp)

    case("missing_argv",
         lambda d: R(d, "argv.json").unlink(), "refused",
         "the invocation was never recorded as tokens", tmp)

    case("blank_attempt",
         lambda d: [f.write_text("\n") for f in
                    [R(d, "attempt")] + [R(d, s2) for s2 in
                     ("exit.attempt", "wall.attempt", "time.attempt",
                      "log.attempt", "cmd.attempt", "argv.json.attempt")]],
         "refused", "malformed attempt id", tmp)

    case("malformed_wall", lambda d: R(d, "wall").write_text("not-a-number\n"),
         "refused", "malformed .wall duration", tmp)
    case("negative_wall", lambda d: R(d, "wall").write_text("-5\n"),
         "refused", "malformed .wall duration", tmp)


    def wrong_input(d):
        p = json.loads((d / "plan.json").read_text())
        p["rows"][0]["cert_sha256"] = "0" * 64
        (d / "plan.json").write_text(json.dumps(p))
    case("wrong_input_hash", wrong_input, "refused", "the row names", tmp)

    def wrong_binary(d):
        # Point the plan at a DIFFERENT binary; the recorded command and the
        # capture both name the real one, so both bindings must object.
        b = d / "other_binary"
        b.write_bytes((SRC / "nat" / "proto_probe").read_bytes() + b"\0")
        b.chmod(0o755)
        p = json.loads((d / "plan.json").read_text())
        p["binary"] = str(b)
        (d / "plan.json").write_text(json.dumps(p))
    case("wrong_binary", wrong_binary, "refused", "is not this row's", tmp)

    case("stale_log",
         lambda d: os.utime(R(d, "cmd"), (R(d, "log").stat().st_mtime + 10,) * 2),
         "refused", "predates the launch marker", tmp)

    def drop_seed(d):
        t = R(d, "log").read_text()
        R(d, "log").write_text(re.sub(r"  seed 7: residual ok[^\n]*\n", "", t))
    case("missing_seed", drop_seed, "refused", "the contract is", tmp)

    def dup_seed(d):
        t = R(d, "log").read_text()
        m = re.search(r"  seed 4: residual ok[^\n]*\n", t)
        R(d, "log").write_text(t.replace(m.group(0), m.group(0) * 2))
    case("duplicate_seed", dup_seed, "refused", "duplicates", tmp)

    def seed_disagrees(d):
        t = R(d, "log").read_text()
        R(d, "log").write_text(t.replace("seed 2: residual ok  matches interpretDesign true",
                                         "seed 2: residual ok  matches interpretDesign false"))
    case("seed_disagrees", seed_disagrees, "refused", "disagreed", tmp)

    def trunc_trace(d):
        t = R(d, "log").read_text()
        R(d, "log").write_text(re.sub(r"trace: 4 cycles", "trace: 1 cycles", t))
    case("truncated_trace", trunc_trace, "refused", "the contract is 4", tmp)

    case("trace_disagrees",
         lambda d: R(d, "log").write_text(R(d, "log").read_text().replace(
             "cycles -- agrees true", "cycles -- agrees false")),
         "refused", "trace_agrees", tmp)
    case("support_false",
         lambda d: R(d, "log").write_text(R(d, "log").read_text().replace(
             "support: wf true", "support: wf false")),
         "refused", "support conjuncts not all true", tmp)
    case("checker_missing",
         lambda d: R(d, "log").write_text(re.sub(r"  checkResidual: ACCEPTED[^\n]*\n", "",
                                                 R(d, "log").read_text())),
         "refused", "no accepted checkResidual bound", tmp)
    case("control_false",
         lambda d: R(d, "log").write_text(R(d, "log").read_text().replace(
             "ok vs interpretDesign true", "ok vs interpretDesign false")),
         "refused", "control is", tmp)
    case("variant_not_applied",
         lambda d: R(d, "log").write_text(R(d, "log").read_text() + "variant not applied\n"),
         "refused", "refusal marker", tmp)
    case("nonzero_after_success_output",
         lambda d: (R(d, "exit").write_text("17\n"),
                    R(d, "time").write_text(R(d, "time").read_text()
                                            .replace("exit 0", "exit 17"))),
         "refused", "process exited 17", tmp)
    case("timeout_stop", lambda d: (R(d, "exit").write_text("124\n"),
                                    R(d, "time").write_text(R(d, "time").read_text()
                                                            .replace("exit 0", "exit 124"))),
         "refused", "RESOURCE STOP", tmp)
    case("signal_stop_not_called_oom",
         lambda d: (R(d, "exit").write_text("-9\n"),
                    R(d, "signal").write_text("9\n"),
                    R(d, "signal.attempt").write_text(R(d, "attempt").read_text())),
         "refused", "no retained cgroup evidence that this was an OOM", tmp)

    # --admit-only must not touch retained evidence
    d = stage(tmp)
    before = {p.name: (p.read_bytes(), p.stat().st_mtime_ns)
              for p in (d / "runs").iterdir()}
    run(d); run(d)
    after = {p.name: (p.read_bytes(), p.stat().st_mtime_ns)
             for p in (d / "runs").iterdir()}
    if before == after:
        ok("admit_only_preserves_evidence", f"{len(before)} files unchanged in bytes and mtime")
    else:
        ch = [k for k in before if before.get(k) != after.get(k)]
        bad("admit_only_preserves_evidence", f"changed: {ch}")

    # capture failure must fail closed: nothing launched, row not admitted
    d = stage(tmp)
    p = json.loads((d / "plan.json").read_text())
    p["capture"] = True
    p["recorder"] = str(d / "no_such_recorder.py")
    p["recorder_runner"] = "file-ab"
    (d / "plan.json").write_text(json.dumps(p))
    r = subprocess.run([sys.executable, "-I", str(CLI), "--plan", str(d / "plan.json"),
                        "--out", str(d / "j2.tsv")], capture_output=True, text=True)
    lines = [l for l in (d / "j2.tsv").read_text().splitlines() if not l.startswith("#")]
    st = dict(zip(lines[0].split("\t"), lines[1].split("\t"))).get("status", "")
    if st == "capture_failed" and "not launched" in (d / "j2.tsv").read_text():
        ok("capture_failure_fails_closed", "no launch, row not admitted")
    else:
        bad("capture_failure_fails_closed", f"status {st!r}")

    shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print(f"\nFP-REPLAY CONTROLS: {len(fails)} failure(s): {fails}")
        return 1
    print("\nFP-REPLAY CONTROLS OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
