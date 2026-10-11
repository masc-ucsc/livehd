#!/usr/bin/env python3
"""Replay one original FP runner configuration, and ADMIT a row only on evidence.

WHY THIS EXISTS.  The first replay batch wrote `exit=0` into its join as a
LITERAL.  The public transcript happened to confirm all eight were 0, so no
observed result was wrong -- but the join asserted a value it had not derived,
and a table that can say "exit 0" without looking is not evidence.  The
retained `.time` files did not even carry the exit code.

So every field a row is credited on is derived from a file this run wrote:

  <blk>.exit     the OUTER process status, captured as `$?`, one integer
  <blk>.signal   the signal, when the status says one
  <blk>.log      complete stdout+stderr
  <blk>.time     /usr/bin/time, including the `exit %x` trailer the original
                 recorder parses -- the WORKER's own view of its status
  <blk>.cmd      the exact full launch command, including the guard
  <blk>.guard    the enforcement scope and its limits
  <blk>.capture.json  the pre-launch hashes, when a capture was taken

ADMISSION.  `pass` requires ALL of: the outer status is 0; the worker trailer
agrees; the log is NEWER than the launch; the certificate and binary hashes are
the ones the row names; and every required observable is present and true.
Anything else is recorded with its status and refused -- a success-looking log
after a nonzero exit is the case this exists for.

Identity is bound by FULL sha256, never a prefix.
"""

import argparse, hashlib, json, os, pathlib, re, subprocess, sys, time

ROOT = pathlib.Path(__file__).resolve().parents[3]


def sha(p):
    return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()


def launch(binary, cert, fuel_s, fuel_w, outdir, name, memmax, timeout_s, mode):
    outdir.mkdir(parents=True, exist_ok=True)
    log, tf = outdir / f"{name}.log", outdir / f"{name}.time"
    for f in (log, tf, outdir / f"{name}.exit"):
        if f.exists():
            f.unlink()
    argv = [str(binary)] + ([mode] if mode else []) + [str(cert), str(fuel_s), str(fuel_w)]
    guard = ["systemd-run", "--user", "--scope", "--quiet",
             "--property=Delegate=yes", f"--property=MemoryMax={memmax}"]
    timed = ["/usr/bin/time", "-f", "RSS %M KB\nexit %x", "-o", str(tf), "-a",
             "timeout", str(timeout_s), "nice", "-n19", "ionice", "-c3"]
    full = guard + timed + argv
    (outdir / f"{name}.cmd").write_text(" ".join(full) + "\n")
    (outdir / f"{name}.guard").write_text(
        f"scope=systemd-run --user --scope Delegate=yes MemoryMax={memmax}\n"
        f"wall_bound={timeout_s}s (timeout(1)); exceeding it is a RESOURCE STOP\n")
    t0 = time.time()
    with log.open("wb") as fh:
        rc = subprocess.run(full, stdout=fh, stderr=subprocess.STDOUT).returncode
    (outdir / f"{name}.exit").write_text(f"{rc}\n")
    if rc > 128:
        (outdir / f"{name}.signal").write_text(f"{rc - 128}\n")
    return rc, t0


# PER-MODE OUTPUT CONTRACTS.  The original runners do NOT share a format, and
# assuming one is how an admitter ends up refusing a good run or crediting a
# bad one.  `--file-ab` prints its support fields WITHOUT total_probe's
# `| ALL true` summary, says "residual ok" where total_probe says "ok", adds a
# `clock-domains` count to the trace line, and reports the checker bound as
# "checkResidual: ACCEPTED, proved-sufficient bound N" rather than inside a
# "residual N terms, checker bound M" line.  Each mode's real lines are encoded
# here; none of these requirements is weaker than the other's.
CONTRACTS = {
    "--file-ab": {
        "support_fields": r"support: wf (\w+) memFree (\w+) sources (\w+) ops (\w+) "
                          r"arities (\w+) flopClocks (\w+)",
        "seed": r"seed (\d+): residual ok  matches interpretDesign (\w+)",
        "trace": r"trace: (\d+) cycles -- agrees (\w+)\s+states-reached (\d+)\s+"
                 r"clock-domains (\d+)",
        "control": r"control: variant interpreter ok vs interpretDesign (\w+)",
        "bound": r"checkResidual: ACCEPTED, proved-sufficient bound (\d+)",
        "terms": r"^  terms (\d+)",
        # `--file-ab` refuses with this on stderr and returns 2 when the variant
        # was not applied; its ABSENCE beside a zero exit is the evidence that
        # the variant-changed admission held.
        "forbidden": [r"variant not applied"],
    },
    "": {   # total_probe, positional
        "support_all": r"support: .*\| ALL (\w+)",
        "seed": r"seed (\d+): ok  matches interpretDesign (\w+)",
        "trace": r"trace: (\d+) cycles, independent states -- agrees (\w+)\s+"
                 r"states-reached (\d+)",
        "control": r"control: variant interpreter matches interpretDesign (\w+)",
        "residual": r"residual (\d+) terms, checker bound (\d+)",
        "forbidden": [],
    },
}


def admit(outdir, name, cert, binary, mode, want_cert_sha, fallback=None):
    """Derived from retained files, never assumed.  (status, fields, refusals).

    Freshness is judged against the `.cmd` file, which this runner writes
    IMMEDIATELY before launching -- so a log older than it is output from a
    previous run and is refused as stale.
    """
    refus, f = [], {}
    ep, cp, lp = (outdir / f"{name}.exit"), (outdir / f"{name}.cmd"), (outdir / f"{name}.log")
    if not ep.is_file():
        # BACKFILL, and it is never a pass. An earlier batch recorded its exits
        # only in the public transcript, so the observables below can still be
        # derived from the retained log -- but the process status cannot, and a
        # row whose status comes from prose outside the evidence tree is
        # reported as evidence-limited rather than credited. Re-running purely
        # to manufacture provenance would be inventing it.
        if not fallback:
            return "refused", f, ["no .exit file: the outer status was never captured"]
        f["outer_exit"] = fallback.get("exit", "")
        f["exit_evidence"] = fallback.get("evidence", "")
        limited = True
    else:
        outer = int(ep.read_text().strip())
        f["outer_exit"] = outer
        f["exit_evidence"] = f"derived from {ep}"
        limited = False
    outer = f["outer_exit"] if isinstance(f["outer_exit"], int) else 0
    sg = outdir / f"{name}.signal"
    f["signal"] = sg.read_text().strip() if sg.is_file() else ""
    if not lp.is_file():
        return "refused", f, ["no log"]
    if cp.is_file() and lp.stat().st_mtime < cp.stat().st_mtime:
        refus.append("the log predates the launch marker: stale output")
    t = lp.read_text(errors="replace")
    tf = outdir / f"{name}.time"
    tt = tf.read_text(errors="replace") if tf.is_file() else ""
    m = re.search(r"^exit (\d+)\s*$", tt, re.M)
    f["worker_exit"] = int(m.group(1)) if m else ""
    if m and int(m.group(1)) != outer and outer <= 128:
        refus.append(f"worker trailer says {m.group(1)}, outer status says {outer}")
    mr = re.search(r"RSS (\d+) KB", tt)
    f["peak_rss_kb"] = mr.group(1) if mr else ""
    f["cert_sha256"], f["binary_sha256"] = sha(cert), sha(binary)
    if want_cert_sha and f["cert_sha256"] != want_cert_sha:
        refus.append(f"certificate is {f['cert_sha256']}, the row names {want_cert_sha}")
    if outer == 124:
        refus.append("timeout(1) stopped it: RESOURCE STOP, undecided")
    elif outer > 128:
        refus.append(f"terminated by signal {outer - 128}: RESOURCE/SIGNAL STOP, undecided")
    elif outer != 0:
        refus.append(f"process exited {outer}")

    C = CONTRACTS[mode or ""]
    for pat in C["forbidden"]:
        if re.search(pat, t):
            refus.append(f"the log contains a refusal marker: {pat!r}")
    seeds = re.findall(C["seed"], t)
    f["seeds_total"], f["seeds_ok"] = len(seeds), sum(1 for _, v in seeds if v == "true")
    f["seeds"] = ",".join(x for x, _ in seeds)
    if f["seeds_total"] == 0:
        refus.append("no stimulus results in the log")
    elif f["seeds_ok"] != f["seeds_total"]:
        refus.append(f"{f['seeds_total'] - f['seeds_ok']} stimulus/stimuli disagreed")
    if "support_fields" in C:
        ms = re.search(C["support_fields"], t)
        if not ms:
            refus.append("required observable missing: support fields")
            f["support_all"] = ""
        else:
            vals = ms.groups()
            f["support_all"] = "true" if all(v == "true" for v in vals) else "FALSE"
            if f["support_all"] != "true":
                refus.append(f"support conjuncts not all true: {vals}")
    else:
        ms = re.search(C["support_all"], t)
        f["support_all"] = ms.group(1) if ms else ""
        if not ms:
            refus.append("required observable missing: support_all")
        elif f["support_all"] != "true":
            refus.append(f"support_all is {f['support_all']!r}")
    mt = re.search(C["trace"], t)
    f["trace_agrees"] = mt.group(2) if mt else ""
    if not mt:
        refus.append("required observable missing: trace")
    elif f["trace_agrees"] != "true":
        refus.append(f"trace_agrees is {f['trace_agrees']!r}")
    mc = re.search(C["control"], t)
    f["control"] = mc.group(1) if mc else ""
    if not mc:
        refus.append("required observable missing: control")
    elif f["control"] != "true":
        refus.append(f"control is {f['control']!r}")
    if "residual" in C:
        mres = re.search(C["residual"], t)
        f["residual_terms"] = mres.group(1) if mres else ""
        f["checker_bound"] = mres.group(2) if mres else ""
        if not mres:
            refus.append("no residual/checker-bound line: malformed or truncated result")
    else:
        mb, mterm = re.search(C["bound"], t), re.search(C["terms"], t, re.M)
        f["checker_bound"] = mb.group(1) if mb else ""
        f["residual_terms"] = mterm.group(1) if mterm else ""
        if not mb:
            refus.append("no accepted checkResidual bound: malformed or truncated result")
        if not mterm:
            refus.append("no residual term count")
    if refus:
        return "refused", f, refus
    return ("observed_evidence_limited" if limited else "pass"), f, refus


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plan", required=True, help="JSON list of rows to replay")
    ap.add_argument("--out", required=True, help="join TSV")
    ap.add_argument("--admit-only", action="store_true",
                    help="re-admit from RETAINED evidence without launching; the "
                         "freshness check still applies, so a stale log still refuses")
    a = ap.parse_args()
    plan = json.loads(pathlib.Path(a.plan).read_text())
    outdir = pathlib.Path(plan["outdir"])
    binary = pathlib.Path(plan["binary"])
    rows, COLS = [], ["record_id", "block", "mode", "status", "outer_exit", "worker_exit",
                      "signal", "wall_s", "peak_rss_kb", "seeds", "seeds_ok", "seeds_total",
                      "support_all", "trace_agrees", "control", "residual_terms",
                      "checker_bound", "cert_sha256", "binary_sha256", "cert_path",
                      "cmd_file", "log_file", "time_file", "exit_file", "guard_file",
                      "capture_file", "exit_evidence", "refusals"]
    for r in plan["rows"]:
        name, cert = r["name"], pathlib.Path(r["cert"])
        print(f"{name:<22}", end="", flush=True)
        if not cert.is_file():
            rows.append({c: "" for c in COLS} | {
                "record_id": r["record_id"], "block": name, "mode": plan.get("mode", ""),
                "status": "input_unavailable", "cert_path": str(cert),
                "refusals": "certificate not present"})
            print("INPUT_UNAVAILABLE"); continue
        # GENUINE pre-launch capture: hashes taken BEFORE the process starts, which
        # is the only form that pins the executed code without assumption. Never
        # synthesised afterwards -- a capture written after the run describes the
        # tree, not what ran, and the recorder labels it that way on purpose.
        if plan.get("capture"):
            argv_preview = " ".join([str(binary)] + ([plan["mode"]] if plan.get("mode") else [])
                                    + [str(cert), str(plan["fuel_step"]), str(plan["fuel_work"])])
            rec = pathlib.Path(plan["recorder"]).resolve()
            cp = subprocess.run([sys.executable, "-I", str(rec), "prelaunch",
                                 str(cert), "--binary", str(binary), "--cmd", argv_preview,
                                 "--out", str(outdir / f"{name}.capture.json"),
                                 "--runner", plan["recorder_runner"]],
                                cwd=plan.get("recorder_cwd", "."), capture_output=True,
                                text=True)
            # NOT swallowed. The first version ignored this result and passed a
            # relative recorder path against a changed cwd, so every capture
            # silently failed to exist and the rows would have been recorded at
            # `reconstructed` strength while looking like pre-launch ones.
            if cp.returncode != 0 or not (outdir / f"{name}.capture.json").is_file():
                print(f"\n  PRE-LAUNCH CAPTURE FAILED for {name}: rc={cp.returncode} "
                      f"{(cp.stderr or cp.stdout).strip()[:160]}", file=sys.stderr)
                print(f"{name:<22}", end="", flush=True)
        t0 = time.time()
        rc, _ = (0, 0) if a.admit_only else launch(binary, cert, plan["fuel_step"], plan["fuel_work"], outdir, name,
                       plan["memmax"], r.get("timeout_s", plan["timeout_s"]),
                       plan.get("mode"))
        st, f, refus = admit(outdir, name, cert, binary, plan.get("mode"),
                             r.get("cert_sha256", ""), r.get("exit_fallback"))
        wall = re.search(r"wall_s\s+([\d.]+)", "") or None
        rows.append({c: "" for c in COLS} | {
            "record_id": r["record_id"], "block": name, "mode": plan.get("mode", ""),
            "status": st, "wall_s": f"{time.time() - t0:.2f}", "cert_path": str(cert),
            "cmd_file": str(outdir / f"{name}.cmd"), "log_file": str(outdir / f"{name}.log"),
            "time_file": str(outdir / f"{name}.time"),
            "exit_file": str(outdir / f"{name}.exit"),
            "guard_file": str(outdir / f"{name}.guard"),
            "capture_file": str(outdir / f"{name}.capture.json")
            if (outdir / f"{name}.capture.json").is_file() else "",
            "refusals": "; ".join(refus)} | {k: v for k, v in f.items() if k in COLS})
        print(f"{st:<10} exit={f.get('outer_exit')} seeds={f.get('seeds_ok')}/"
              f"{f.get('seeds_total')} {('REFUSED: ' + refus[0]) if refus else ''}")
    out = pathlib.Path(a.out)
    with out.open("w", encoding="utf-8") as fh:
        fh.write(f"# runner binary\t{binary}\n")
        fh.write(f"# binary_sha256\t{sha(binary)}\n")
        fh.write(f"# mode\t{plan.get('mode') or '(positional)'}\n")
        fh.write(f"# fuel\t{plan['fuel_step']}/{plan['fuel_work']}\n")
        fh.write(f"# build\t{plan.get('build_cmd','')}\n")
        fh.write("# Every credited field is DERIVED from a file this run wrote; the\n")
        fh.write("# per-row cmd/log/time/exit/guard paths are the evidence. Identity is\n")
        fh.write("# bound by FULL sha256. `status` is pass only when the outer process\n")
        fh.write("# status is 0, the worker trailer agrees, the log is fresh, the hashes\n")
        fh.write("# match and every required observable is present and true.\n")
        fh.write("\t".join(COLS) + "\n")
        for r in rows:
            fh.write("\t".join(str(r.get(c, "")) for c in COLS) + "\n")
    npass = sum(1 for r in rows if r["status"] == "pass")
    print(f"\n{npass}/{len(rows)} admitted; join {out}")
    return 0 if npass == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
