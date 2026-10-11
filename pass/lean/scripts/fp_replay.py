#!/usr/bin/env python3
"""Replay one original FP runner configuration, and ADMIT a row only on evidence.

WHY THIS IS STRICT.  Two earlier versions were not.  The first wrote `exit=0`
into its join as a LITERAL.  The second derived the status but still admitted a
row with no worker timing, no launch marker, ONE stimulus, ONE trace cycle, a
command that did not match the row, and a capture naming a different binary.
Retained CLI controls showed seven such admissions, so the boundary is restated
here as explicit requirements rather than as whatever the parser happened to
look at.

ADMISSION.  `pass` requires ALL of:

  * `.attempt` present, and EVERY other evidence file carrying the same attempt
    id -- so a status or signal file left by an earlier attempt cannot be read
    as this one's;
  * the outer status present and UNIQUELY parsed, and zero;
  * the worker's own `exit` trailer present, uniquely parsed, and agreeing;
  * a launch marker present, and the log newer than it;
  * the recorded command naming this binary, this mode, this certificate and
    BOTH fuel numbers;
  * a prelaunch capture that parses, names this binary and this certificate by
    full sha256, and predates the launch marker;
  * exactly the six stimuli 0,1,2,3,4,7 -- no missing, no duplicates -- all
    matching;
  * a four-cycle trace that agrees;
  * the mode's own support, checker and control contracts.

Anything missing or malformed REFUSES with the reason.  A signal stop is a
signal stop: it is only called an OOM when retained cgroup `memory.events`
says `oom_kill` was nonzero.

Timing: `wall_s` is the RUN's duration, written at launch into `.wall`.  It is
never the parser's own elapsed time, which is what re-admission would otherwise
report.  Guard metadata is kept as REQUESTED and OBSERVED separately; absent
observations stay absent rather than being backfilled.
"""

import argparse, hashlib, json, os, pathlib, re, subprocess, sys, time, uuid

ROOT = pathlib.Path(__file__).resolve().parents[3]
WANT_SEEDS = ["0", "1", "2", "3", "4", "7"]
WANT_TRACE_CYCLES = 4


def sha(p):
    return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()


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
        "forbidden": [r"variant not applied"],
    },
    "": {
        "support_all": r"support: .*\| ALL (\w+)",
        "seed": r"seed (\d+): ok  matches interpretDesign (\w+)",
        "trace": r"trace: (\d+) cycles, independent states -- agrees (\w+)\s+"
                 r"states-reached (\d+)",
        "control": r"control: variant interpreter matches interpretDesign (\w+)",
        "residual": r"residual (\d+) terms, checker bound (\d+)",
        "forbidden": [],
    },
}


def capture(plan, binary, cert, outdir, name, attempt):
    """Genuine pre-launch capture, or a refusal.  Returns (ok, detail).

    The output directory is created HERE. It used to be created inside
    `launch`, which runs afterwards, so against a fresh outdir the recorder had
    nowhere to write and every capture failed -- and because capture failure
    fails closed, a whole batch refused instead of running uncaptured. The
    guard did its job; the ordering was the bug.
    """
    outdir.mkdir(parents=True, exist_ok=True)
    rec = pathlib.Path(plan["recorder"]).resolve()
    argv = " ".join([str(binary)] + ([plan["mode"]] if plan.get("mode") else [])
                    + [str(cert), str(plan["fuel_step"]), str(plan["fuel_work"])])
    dst = outdir / f"{name}.capture.json"
    cp = subprocess.run([sys.executable, "-I", str(rec), "prelaunch", str(cert),
                         "--binary", str(binary), "--cmd", argv, "--out", str(dst),
                         "--runner", plan["recorder_runner"]],
                        cwd=plan.get("recorder_cwd", "."), capture_output=True, text=True)
    if cp.returncode != 0 or not dst.is_file():
        return False, f"rc={cp.returncode} {(cp.stderr or cp.stdout).strip()[:200]}"
    (outdir / f"{name}.capture.attempt").write_text(attempt + "\n")
    return True, ""


def launch(binary, cert, plan, outdir, name, timeout_s, attempt):
    outdir.mkdir(parents=True, exist_ok=True)
    for suf in ("log", "time", "exit", "signal", "wall", "cmd", "guard.json",
                "argv.json", "attempt"):
        f = outdir / f"{name}.{suf}"
        if f.exists():
            f.unlink()
    argv = [str(binary)] + ([plan["mode"]] if plan.get("mode") else []) \
        + [str(cert), str(plan["fuel_step"]), str(plan["fuel_work"])]
    memmax = plan["memmax"]
    # NAME the scope. Without --unit the only pid we hold is systemd-run's own
    # client, which lives in the SESSION scope -- so sampling from it read the
    # whole session's cgroup (memory.max "max", a 128 GB peak) and reported it
    # as if it were this run's guard. With a unit name the scope's cgroup path
    # is deterministic and can be read directly.
    unit = f"fpreplay-{name}-{attempt}"
    guard = ["systemd-run", "--user", "--scope", "--quiet", f"--unit={unit}",
             "--property=Delegate=yes", f"--property=MemoryMax={memmax}"]
    timed = ["/usr/bin/time", "-f", "RSS %M KB\nexit %x", "-o",
             str(outdir / f"{name}.time"), "-a",
             "timeout", str(timeout_s), "nice", "-n19", "ionice", "-c3"]
    full = guard + timed + argv
    (outdir / f"{name}.attempt").write_text(attempt + "\n")
    (outdir / f"{name}.cmd").write_text(" ".join(full) + "\n")
    # STRUCTURED argv. The wrapper command contains a literal newline inside the
    # /usr/bin/time format, so splitting the `.cmd` string on whitespace does not
    # recover the tokens -- and substring tests on it accept `12000000` as
    # `200000`. The runner argv is recorded as a list and checked token by token.
    (outdir / f"{name}.argv.json").write_text(json.dumps({
        "attempt": attempt, "wrapper_argv": full, "runner_argv": argv,
        "binary": str(binary), "mode": plan.get("mode"), "cert": str(cert),
        "fuel_step": plan["fuel_step"], "fuel_work": plan["fuel_work"]}, indent=1))
    (outdir / f"{name}.guard.json").write_text(json.dumps({
        "attempt": attempt,
        "requested": {"MemoryMax": memmax, "wall_timeout_s": timeout_s, "unit": unit,
                      "scope": "systemd-run --user --scope Delegate=yes"},
        "observed": {},
    }, indent=1))
    t0 = time.time()
    observed = {}
    with (outdir / f"{name}.log").open("wb") as fh:
        proc = subprocess.Popen(full, stdout=fh, stderr=subprocess.STDOUT)
        # OBSERVE THE GUARD WHILE THE SCOPE EXISTS. The scope's cgroup is torn
        # down when the process exits, so `memory.max`, `memory.peak` and
        # `memory.events` have to be read during the run. An empty
        # `guard_observed` is the ABSENCE of an observation, not evidence that
        # the limit was enforced, so this samples rather than assuming.
        uid = os.getuid()
        cg = pathlib.Path(f"/sys/fs/cgroup/user.slice/user-{uid}.slice/"
                          f"user@{uid}.service/app.slice/{unit}.scope")
        while proc.poll() is None:
            if not (cg / "memory.max").is_file():
                time.sleep(0.2)
                continue
            if True:
                for k in ("memory.max", "memory.peak", "memory.current"):
                    try:
                        v = (cg / k).read_text().strip()
                        if k == "memory.peak":
                            observed[k] = max(int(v), int(observed.get(k, 0)))
                        else:
                            observed[k] = v
                    except (OSError, ValueError):
                        pass
                try:
                    for ln in (cg / "memory.events").read_text().split("\n"):
                        if ln.strip():
                            kk, vv = ln.split()
                            observed[f"memory.events.{kk}"] = vv
                except (OSError, ValueError):
                    pass
                observed["cgroup"] = str(cg)
                observed["unit"] = unit
                try:
                    observed["scope_pids"] = sorted(
                        (cg / "cgroup.procs").read_text().split())
                except OSError:
                    pass
            time.sleep(0.25)
        rc = proc.wait()
    wall = time.time() - t0
    # subprocess returns a NEGATIVE value when the child died on a signal.
    sig = -rc if rc < 0 else (rc - 128 if rc > 128 else 0)
    (outdir / f"{name}.exit").write_text(f"{rc}\n")
    (outdir / f"{name}.wall").write_text(f"{wall:.2f}\n")
    if sig:
        (outdir / f"{name}.signal").write_text(f"{sig}\n")
    gp = outdir / f"{name}.guard.json"
    g = json.loads(gp.read_text())
    g["observed"] = observed
    gp.write_text(json.dumps(g, indent=1))
    for suf in ("exit", "wall", "signal", "time", "log", "cmd", "argv.json",
                "guard.json"):
        a = outdir / f"{name}.{suf}.attempt"
        if (outdir / f"{name}.{suf}").is_file():
            a.write_text(attempt + "\n")
    return rc


def one(v, pat, text, flags=0):
    """Exactly one match, or None.  A second match is malformed evidence."""
    ms = re.findall(pat, text, flags)
    return ms[0] if len(ms) == 1 else None


def admit(outdir, name, cert, binary, plan, want_cert_sha, allow_missing_capture=False):
    mode = plan.get("mode") or ""
    refus, f = [], {"mode": mode or "(positional)"}
    P = lambda suf: outdir / f"{name}.{suf}"

    ATTEMPT_RE = re.compile(r"^\d{8}T\d{6}-[0-9a-f]{8}$")
    attempt = ""
    if not P("attempt").is_file():
        refus.append("no .attempt id: this evidence is not scoped to a run")
    else:
        attempt = P("attempt").read_text().strip()
        f["attempt"] = attempt
        # A BLANK id matched every sidecar that was also blank, so unscoped
        # evidence compared equal to itself and passed.
        if not ATTEMPT_RE.fullmatch(attempt):
            refus.append(f"malformed attempt id {attempt!r}")
            attempt = ""
    for suf in ("exit", "wall", "time", "log", "cmd"):
        ap = P(f"{suf}.attempt")
        if not P(suf).is_file():
            refus.append(f"missing evidence file: .{suf}")
        elif attempt and (not ap.is_file()
                          or ap.read_text().strip() != attempt
                          or not ap.read_text().strip()):
            refus.append(f".{suf} is not scoped to attempt {attempt}: it may be an "
                         f"earlier run's file")
    # Only a MISSING log or status stops the parse. An authentication failure
    # does not: the observables are still worth collecting, so a run recorded
    # before this standard existed can keep its comparison data under
    # `--observed-only` instead of arriving empty.
    if not P("log").is_file():
        return "refused", f, refus + ["no log"]

    outer = None
    if P("exit").is_file():
        raw = P("exit").read_text().strip()
        if not re.fullmatch(r"-?\d+", raw):
            return "refused", f, refus + [f"malformed outer status {raw!r}"]
        outer = int(raw)
        f["outer_exit"] = outer
    # A missing status is NOT backfilled from anywhere. The field stays empty
    # and the refusal above already says why; the observables below are still
    # collected, so the run's comparison survives without a status being
    # invented for it.
    f["signal"] = P("signal").read_text().strip() if P("signal").is_file() else ""
    f["wall_s"] = ""
    if P("wall").is_file():
        raw_w = P("wall").read_text().strip()
        try:
            wv = float(raw_w)
            if wv < 0 or wv != wv or wv in (float("inf"), float("-inf")):
                raise ValueError(raw_w)
            f["wall_s"] = raw_w
        except ValueError:
            refus.append(f"malformed .wall duration {raw_w!r}")
    f["exit_evidence"] = f"derived from {P('exit')}"

    tt = P("time").read_text(errors="replace") if P("time").is_file() else ""
    w = one(None, r"^exit (\d+)\s*$", tt, re.M)
    if w is None:
        refus.append("no uniquely parsed worker `exit` trailer in .time")
        f["worker_exit"] = ""
    else:
        f["worker_exit"] = int(w)
        if outer is not None and 0 <= outer <= 128 and int(w) != outer:
            refus.append(f"worker trailer says {w}, outer status says {outer}")
    rss = one(None, r"RSS (\d+) KB", tt)
    f["peak_rss_kb"] = rss or ""
    if rss is None:
        refus.append("no uniquely parsed RSS in .time")

    lp, cp = P("log"), P("cmd")
    if cp.is_file() and lp.stat().st_mtime < cp.stat().st_mtime:
        refus.append("the log predates the launch marker: stale output")
    # Binding checks apply only when there IS a recorded command. Running them
    # against an absent file produced four "does not name this ..." refusals
    # that read as contract violations when the only fact is that the file is
    # missing -- which the evidence refusal above already states.
    cmd = cp.read_text() if cp.is_file() else ""
    f["cmd"] = cmd.strip()
    avp = P("argv.json")
    if not avp.is_file():
        refus.append("no .argv.json: the invocation was never recorded as tokens")
    else:
        try:
            av = json.loads(avp.read_text())
        except (OSError, json.JSONDecodeError) as e:
            av = None
            refus.append(f"the recorded argv does not parse: {e}")
        if av is not None:
            aap = P("argv.json.attempt")
            if attempt and (not aap.is_file() or aap.read_text().strip() != attempt):
                refus.append("the recorded argv is not scoped to this attempt")
            want = [str(binary)] + ([mode] if mode else []) \
                + [str(cert), str(plan["fuel_step"]), str(plan["fuel_work"])]
            got = [str(x) for x in (av.get("runner_argv") or [])]
            f["runner_argv"] = " ".join(got)
            # TOKEN BY TOKEN, and in position. A substring test accepted a step
            # fuel of 12000000 as 200000 because the digits appear inside it.
            if got != want:
                refus.append(f"recorded argv {got} is not this row's {want}")

    capf = P("capture.json")
    f["capture_file"] = str(capf) if capf.is_file() else ""
    if not capf.is_file():
        # Even when explicitly allowed, an absent capture caps the row at
        # evidence-limited: nothing pins the executed code. The flag chooses
        # between "refused" and "kept as an observation", never "credited".
        f["capture_state"] = "ABSENT"
        refus.append("no pre-launch capture"
                     + (" (allowed: row is capped at evidence-limited)"
                        if allow_missing_capture else ""))
        # Only the EXPLICIT flag turns an absent capture into a retained
        # observation. Without it, a missing capture refuses.
        if allow_missing_capture:
            f["capture_absent"] = "1"
    else:
        capa = P("capture.attempt")
        try:
            cap = json.loads(capf.read_text())
        except (OSError, json.JSONDecodeError) as e:
            cap = None
            refus.append(f"the capture does not parse: {e}")
        if cap is not None:
            if not capa.is_file() or capa.read_text().strip() != attempt:
                refus.append("the capture is not scoped to this attempt")
            if cp.is_file() and capf.stat().st_mtime > cp.stat().st_mtime:
                refus.append("the capture is NEWER than the launch marker: it does "
                             "not describe what ran")
            # EXPLICIT FIELDS. Searching the JSON text accepted a capture whose
            # binary_sha256 and cert_sha256 were wrong but whose notes happened
            # to mention the expected hashes.
            cb, cc = cap.get("binary_sha256"), cap.get("cert_sha256")
            if cb != sha(binary):
                refus.append(f"capture binary_sha256 is {cb!r}, this binary is "
                             f"{sha(binary)}")
            if cc != sha(cert):
                refus.append(f"capture cert_sha256 is {cc!r}, this certificate is "
                             f"{sha(cert)}")
            if cap.get("evidence") != "captured-pre-launch":
                refus.append(f"capture evidence is {cap.get('evidence')!r}, not "
                             f"'captured-pre-launch'")
            f["capture_state"] = "pre-launch, fields validated"

    f["cert_sha256"], f["binary_sha256"] = sha(cert), sha(binary)
    if want_cert_sha and f["cert_sha256"] != want_cert_sha:
        refus.append(f"certificate is {f['cert_sha256']}, the row names {want_cert_sha}")

    gj = P("guard.json")
    if gj.is_file():
        g = json.loads(gj.read_text())
        f["guard_requested"] = json.dumps(g.get("requested", {}))
        f["guard_observed"] = json.dumps(g.get("observed", {}))
    oomk = ""
    if gj.is_file():
        oomk = str((json.loads(gj.read_text()).get("observed") or {}).get("oom_kill", ""))
    if outer is None:
        pass
    elif outer == 124:
        refus.append("timeout(1) stopped it: RESOURCE STOP, undecided")
    elif f["signal"]:
        refus.append(f"terminated by signal {f['signal']}: SIGNAL STOP, undecided"
                     + ("; cgroup memory.events records an oom_kill"
                        if oomk not in ("", "0") else
                        "; no retained cgroup evidence that this was an OOM"))
    elif outer != 0:
        refus.append(f"process exited {outer}")

    t = lp.read_text(errors="replace")
    C = CONTRACTS[mode]
    for pat in C["forbidden"]:
        if re.search(pat, t):
            refus.append(f"the log contains a refusal marker: {pat!r}")
    seeds = re.findall(C["seed"], t)
    got = [s for s, _ in seeds]
    f["seeds"] = ",".join(got)
    f["seeds_total"], f["seeds_ok"] = len(seeds), sum(1 for _, v in seeds if v == "true")
    if got != WANT_SEEDS:
        dup = [x for x in set(got) if got.count(x) > 1]
        refus.append(f"stimuli are {got}, the contract is {WANT_SEEDS}"
                     + (f" (duplicates {dup})" if dup else ""))
    if f["seeds_ok"] != f["seeds_total"]:
        refus.append(f"{f['seeds_total'] - f['seeds_ok']} stimulus/stimuli disagreed")

    if "support_fields" in C:
        ms = re.search(C["support_fields"], t)
        f["support_all"] = ("true" if ms and all(v == "true" for v in ms.groups())
                            else ("FALSE" if ms else ""))
        if not ms:
            refus.append("required observable missing: support fields")
        elif f["support_all"] != "true":
            refus.append(f"support conjuncts not all true: {ms.groups()}")
    else:
        ms = re.search(C["support_all"], t)
        f["support_all"] = ms.group(1) if ms else ""
        if not ms:
            refus.append("required observable missing: support_all")
        elif f["support_all"] != "true":
            refus.append(f"support_all is {f['support_all']!r}")

    mt = re.search(C["trace"], t)
    if not mt:
        refus.append("required observable missing: trace")
        f["trace_agrees"], f["trace_cycles"] = "", ""
    else:
        f["trace_cycles"], f["trace_agrees"] = mt.group(1), mt.group(2)
        if int(mt.group(1)) != WANT_TRACE_CYCLES:
            refus.append(f"trace is {mt.group(1)} cycles, the contract is "
                         f"{WANT_TRACE_CYCLES}")
        if f["trace_agrees"] != "true":
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
            refus.append("no residual/checker-bound line: malformed or truncated")
    else:
        mb = re.search(C["bound"], t)
        mterm = re.search(C["terms"], t, re.M)
        f["checker_bound"] = mb.group(1) if mb else ""
        f["residual_terms"] = mterm.group(1) if mterm else ""
        if not mb:
            refus.append("no accepted checkResidual bound: malformed or truncated")
        if not mterm:
            refus.append("no residual term count")

    # EVIDENCE-class refusals say the run cannot be AUTHENTICATED to this
    # attempt; OBSERVABLE-class ones say the run did not do what the contract
    # requires. They are different claims and `--observed-only` separates them,
    # so a batch recorded before this standard existed can keep its observations
    # without being credited. It is opt-in: by default missing or malformed
    # evidence refuses, which is what the controls check.
    f["evidence_class_refusals"] = "; ".join(
        r for r in refus if any(k in r for k in EVIDENCE_MARKERS))
    f["observable_class_refusals"] = "; ".join(
        r for r in refus if not any(k in r for k in EVIDENCE_MARKERS))
    return ("pass" if not refus else "refused"), f, refus


EVIDENCE_MARKERS = (".attempt id", "not scoped to", "missing evidence file",
                    "pre-launch capture", "capture does not", "capture is NEWER",
                    "uniquely parsed", "malformed outer status", "malformed attempt id",
                    ".argv.json", "recorded argv", "capture binary_sha256",
                    "capture cert_sha256", "capture evidence", "malformed .wall")


COLS = ["record_id", "block", "mode", "status", "attempt", "outer_exit", "worker_exit",
        "signal", "wall_s", "peak_rss_kb", "seeds", "seeds_ok", "seeds_total",
        "trace_cycles", "trace_agrees", "support_all", "control", "residual_terms",
        "checker_bound", "cert_sha256", "binary_sha256", "cert_path", "capture_state",
        "guard_requested", "guard_observed", "cmd", "cmd_file", "log_file", "time_file",
        "exit_file", "wall_file", "guard_file", "capture_file", "argv_file",
        "runner_argv", "exit_evidence",
        "evidence_class_refusals", "observable_class_refusals", "refusals"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--plan", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--admit-only", action="store_true",
                    help="re-admit from RETAINED evidence. Launches nothing and takes "
                         "no capture, so the retained bytes and timestamps are left "
                         "exactly as they are.")
    ap.add_argument("--observed-only", action="store_true",
                    help="for rows whose ONLY refusals are evidence-authentication "
                         "ones, report observed_evidence_limited and keep the "
                         "observables, instead of discarding them. Never `pass`.")
    ap.add_argument("--allow-missing-capture", action="store_true",
                    help="admit a row whose capture is absent, marking it so. Without "
                         "this a missing capture refuses.")
    a = ap.parse_args()
    plan = json.loads(pathlib.Path(a.plan).read_text())
    outdir, binary = pathlib.Path(plan["outdir"]), pathlib.Path(plan["binary"])
    rows = []
    for r in plan["rows"]:
        name, cert = r["name"], pathlib.Path(r["cert"])
        print(f"{name:<22}", end="", flush=True)
        base = {c: "" for c in COLS} | {"record_id": r["record_id"], "block": name,
                                        "cert_path": str(cert),
                                        "mode": plan.get("mode") or "(positional)"}
        if not cert.is_file():
            rows.append(base | {"status": "input_unavailable",
                                "refusals": "certificate not present"})
            print("INPUT_UNAVAILABLE"); continue
        if not a.admit_only:
            attempt = f"{time.strftime('%Y%m%dT%H%M%S')}-{uuid.uuid4().hex[:8]}"
            if plan.get("capture"):
                ok, why = capture(plan, binary, cert, outdir, name, attempt)
                if not ok:
                    # FAIL CLOSED. The previous version printed the failure and
                    # launched anyway, so the row would have been admitted with no
                    # capture at all while looking like a pre-launch one.
                    rows.append(base | {"status": "capture_failed", "attempt": attempt,
                                        "refusals": f"pre-launch capture failed: {why}; "
                                                    f"not launched"})
                    print(f"CAPTURE_FAILED  {why[:70]}"); continue
            launch(binary, cert, plan, outdir, name,
                   r.get("timeout_s", plan["timeout_s"]), attempt)
        st, f, refus = admit(outdir, name, cert, binary, plan, r.get("cert_sha256", ""),
                             a.allow_missing_capture)
        if (st == "refused" and (a.observed_only or f.get("capture_absent"))
                and not f.get("observable_class_refusals")):
            st = "observed_evidence_limited"
        rows.append(base | {k: v for k, v in f.items() if k in COLS} | {
            "status": st, "refusals": "; ".join(refus),
            "cmd_file": str(outdir / f"{name}.cmd"), "log_file": str(outdir / f"{name}.log"),
            "time_file": str(outdir / f"{name}.time"),
            "exit_file": str(outdir / f"{name}.exit"),
            "wall_file": str(outdir / f"{name}.wall"),
            "guard_file": str(outdir / f"{name}.guard.json"),
            "argv_file": str(outdir / f"{name}.argv.json")})
        print(f"{st:<10} exit={f.get('outer_exit','')} seeds={f.get('seeds','')} "
              f"{('REFUSED: ' + refus[0][:70]) if refus else ''}")
    out = pathlib.Path(a.out)
    with out.open("w", encoding="utf-8") as fh:
        fh.write(f"# runner binary\t{binary}\n# binary_sha256\t{sha(binary)}\n")
        fh.write(f"# mode\t{plan.get('mode') or '(positional)'}\n")
        fh.write(f"# fuel\t{plan['fuel_step']}/{plan['fuel_work']}\n")
        fh.write(f"# build\t{plan.get('build_cmd','')}\n")
        fh.write("# Every credited field is DERIVED from a file of THIS attempt; the\n")
        fh.write("# attempt id scopes them, so an earlier run's status cannot be read\n")
        fh.write("# as this one's. Identity is bound by FULL sha256. wall_s is the RUN's\n")
        fh.write("# duration from .wall, never the parser's elapsed time. Guard metadata\n")
        fh.write("# is REQUESTED and OBSERVED separately; absent observations stay absent.\n")
        fh.write("\t".join(COLS) + "\n")
        for r in rows:
            # A field must not carry a tab or a newline: the recorded command
            # legitimately contains one (`-f "RSS %M KB\nexit %x"`), and writing
            # it raw split the row in two, so a reader picked up the tail of the
            # command as the first column of a second record.
            fh.write("\t".join(
                str(r.get(c, "")).replace("\t", "\\t").replace("\n", "\\n")
                for c in COLS) + "\n")
    npass = sum(1 for r in rows if r["status"] == "pass")
    print(f"\n{npass}/{len(rows)} admitted; join {out}")
    return 0 if npass == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
