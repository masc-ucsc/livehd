#!/usr/bin/env python3
"""Compare the proof tools' simulator routes (sim:fp, sim:ir, sim:vc) on any benchmark.

A RUN FILE names what to run. One run per line, tab-separated, `#` starts a comment:

    <bench>  <run>  <driver>  <arguments>

`<driver>` is a sym_verify or sym_refmap binary (or any command printing the same lines).
`<arguments>` may use these placeholders:
    {sim}   the route under test (fp, ir or vc)
    {work}  a fresh work directory for this run, route and repetition
    {dir}   the run file's directory (for relative certificate paths)
Example (hpdcache):

    hpdcache  inv  $SYM_NAT/sym_verify  {dir}/lean/cache_c3.dcert conj:{dir}/inv.txt asm:env_ok sf:20000000 wf:500000 sim:{sim} solver:$SYM_NAT/cadical work:{work} jobs:8
    hpdcache  map  $SYM_NAT/sym_refmap  refmap {dir}/map.refmap sf:20000000 wf:500000 sim:{sim} solver:$SYM_NAT/cadical work:{work} jobs:8

`$VAR`s are expanded from the environment. A run must start fresh, so arguments containing
`cache` are refused.

Each (run, route, repetition) runs under
    systemd-run --user --scope -p MemoryMax=<mem>  taskset -c <cpus>  nice -n19 ionice -c3
    /usr/bin/time -v  <driver> <arguments>
The repetitions are interleaved over the routes (fp ir vc fp ir vc ...), so a slow period of the
host does not hit one route only.

MEASURED, per repetition (from the driver's log and from /usr/bin/time):
    setup_ms     specialize (fp), none or checkDesign (ir), compileDesign (vc)
    build_ms     building the symbolic runs / obligations
    clauses      CNF clauses, summed over all obligations solved
    blast_ms     bit-blast + CNF + write, summed over obligations
    cadical_ms   solver time, summed over obligations (with jobs:N > 1 this is a sum of
                 parallel times, not wall time)
    lrat_ms      the verified check (LRAT check of every proof)
    wall_s       the whole run, wall clock
    rss_mb       peak resident memory of the driver (the largest of the driver and the solver
                 runs it waited for, not their sum)
    verdict      the last `RESULT:` line
A driver line `[time] <phase> <ms>` (phase = setup, build, blast, cadical, lrat; `[time] clauses
<n>` for the count) overrides the parsed value for that phase, so new driver modes only need to
print it.

OUTPUT, in --out:
    logs/<bench>.<run>.<sim>.r<k>.{log,time}   raw driver log and /usr/bin/time report
    raw.tsv        one row per repetition
    summary.tsv    the median over repetitions, per (bench, run, route)
    summary.md     the same as a Markdown table; a run whose routes disagree on the verdict is
                   marked VERDICTS DIFFER
    env.tsv        driver sha256s, lean and cadical versions, options (commit this)
    host.txt       the host's CPU, memory and kernel (do NOT commit: machine information)

Usage:
    sym_sim_perf.py <run file> --out <dir> [--sims fp,ir,vc] [--reps 3] [--cpus 0-7]
                    [--mem 48G] [--timeout 6h] [--only <bench>[:<run>]] [--dry-run] [--keep-work]

Each run's work directory (its CNF and LRAT files, gigabytes on a large design) is deleted
once the run's numbers are read from its log; --keep-work keeps them. The logs stay.
"""
import argparse
import hashlib
import os
import shutil
import re
import shlex
import statistics
import subprocess
import sys
import time
from pathlib import Path

METRICS = ["setup_ms", "build_ms", "clauses", "blast_ms", "cadical_ms", "lrat_ms", "wall_s", "rss_mb"]

# Parsed from the drivers' existing lines. Each regex's group 1 is the number.
SETUP_RE = [r"specialized.*? and checked in (\d+) ms", r"specialized and checked in (\d+) ms",
            r"compiled .*?in (\d+) ms", r"design checked .*?in (\d+) ms"]
BUILD_RE = [r"obligations built in (\d+) ms", r"symbolic runs built in (\d+) ms",
            r"^\s*runs: .* in (\d+) ms"]
LRAT_RE = [r"verified check .*? in (\d+) ms"]
# one obligation: "[tag] N clauses; ..." / "base: N clauses, ..." / "step: N clauses; ..."
OBL_CLAUSES = re.compile(r"(?:\]|base:|step:) (\d+) clauses[;,]")
INV_SUMMARY = re.compile(r"invariant: .*PROVED \((\d+) clauses\)")
BLAST = re.compile(r"bit-blast\+CNF\+write (\d+) ms")
SOLVER = re.compile(r"solver (\d+) ms")
TIMELINE = re.compile(r"^\s*\[time\] (\w+) (\d+)\s*$")
RESULT = re.compile(r"RESULT: (.*)$")


def parse_log(text: str) -> dict:
    m = {k: None for k in METRICS}
    m["verdict"] = "-"
    lines = text.splitlines()

    def first(res):
        for line in lines:
            for r in res:
                g = re.search(r, line)
                if g:
                    return int(g.group(1))
        return None

    def total(res):
        vals = [int(g.group(1)) for line in lines for r in res for g in [re.search(r, line)] if g]
        return sum(vals) if vals else None

    m["setup_ms"] = first(SETUP_RE)
    m["build_ms"] = total(BUILD_RE)
    m["lrat_ms"] = total(LRAT_RE)
    obl = [int(g.group(1)) for line in lines for g in [OBL_CLAUSES.search(line)] if g]
    inv = [int(g.group(1)) for line in lines for g in [INV_SUMMARY.search(line)] if g]
    m["clauses"] = (sum(obl) + sum(inv)) if (obl or inv) else None
    bl = [int(g.group(1)) for line in lines for g in [BLAST.search(line)] if g]
    m["blast_ms"] = sum(bl) if bl else None
    so = [int(g.group(1)) for line in lines for g in [SOLVER.search(line)] if g]
    m["cadical_ms"] = sum(so) if so else None
    over = {}
    for line in lines:
        g = TIMELINE.match(line)
        if g:
            over[g.group(1)] = over.get(g.group(1), 0) + int(g.group(2))
    for ph in ["setup", "build", "blast", "cadical", "lrat"]:
        if ph in over:
            m[f"{ph}_ms"] = over[ph]
    if "clauses" in over:
        m["clauses"] = over["clauses"]
    for line in lines:
        g = RESULT.search(line)
        if g:
            m["verdict"] = g.group(1).strip()
    return m


def parse_time(text: str) -> tuple:
    wall = rss = None
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("Elapsed (wall clock) time"):
            t = line.rsplit(" ", 1)[-1]
            parts = [float(p) for p in t.split(":")]
            wall = sum(p * 60 ** k for k, p in enumerate(reversed(parts)))
        elif line.startswith("Maximum resident set size"):
            rss = int(line.rsplit(" ", 1)[-1]) / 1024.0
    return wall, rss


def read_runs(path: Path, only):
    runs = []
    for n, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) != 4:
            sys.exit(f"{path}:{n}: expected 4 tab-separated columns (bench, run, driver, arguments)")
        bench, run, drv, args = (c.strip() for c in cols)
        if only and not (only == bench or only == f"{bench}:{run}"):
            continue
        if re.search(r"(^|\s)cache(\s|$|:)", args):
            sys.exit(f"{path}:{n}: the arguments use `cache`; a timed run must start fresh")
        runs.append((bench, run, drv, args))
    return runs


def sha256(path: str) -> str:
    try:
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for b in iter(lambda: f.read(1 << 20), b""):
                h.update(b)
        return h.hexdigest()
    except OSError:
        return "-"


def capture(cmd) -> str:
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=60).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return "-"


def fmt(v, k):
    if v is None:
        return "-"
    if k == "wall_s":
        return f"{v:.1f}"
    if k == "rss_mb":
        return f"{v:.0f}"
    return f"{int(v)}"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runfile", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--sims", default="fp,ir,vc")
    ap.add_argument("--reps", type=int, default=3)
    ap.add_argument("--cpus", default="0-7")
    ap.add_argument("--mem", default="48G")
    ap.add_argument("--timeout", default="6h", help="per repetition, as for `timeout`")
    ap.add_argument("--only", default=None)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--summarize", action="store_true",
                    help="rebuild summary.tsv / summary.md from --out's raw.tsv; run nothing")
    ap.add_argument("--keep-work", action="store_true",
                    help="keep each run's work directory (CNF and LRAT files); deleted by default")
    a = ap.parse_args()

    if a.summarize:
        out = a.out.resolve()
        summarize(out, read_raw(out / "raw.tsv"), a.reps, len(expand_cpus(a.cpus)), a.mem)
        return
    runfile = a.runfile.resolve()
    runs = read_runs(runfile, a.only)
    sims = [s.strip() for s in a.sims.split(",") if s.strip()]
    out = a.out.resolve()
    (out / "logs").mkdir(parents=True, exist_ok=True)
    tmp = os.environ.get("TMPDIR", "")

    # environment records: env.tsv (committable) and host.txt (not)
    drivers = sorted({os.path.expandvars(d) for _, _, d, _ in runs})
    with open(out / "env.tsv", "w") as f:
        f.write("key\tvalue\n")
        f.write(f"run_file\t{runfile.name}\n")
        f.write(f"run_file_sha256\t{sha256(str(runfile))}\n")
        for d in drivers:
            f.write(f"driver\t{Path(d).name}\t{sha256(d)}\n")
        # inside formal/lean: elsewhere elan picks its default toolchain, not the pin
        lean_dir = Path(__file__).resolve().parents[4] / "formal" / "lean"
        f.write(f"lean\t{capture(['bash', '-c', f'cd {shlex.quote(str(lean_dir))} && lean --version'])}\n")
        for _, _, _, args in runs:
            for tok in shlex.split(os.path.expandvars(args)):
                if tok.startswith("solver:"):
                    f.write(f"solver\t{capture([tok[7:], '--version'])}\t{sha256(tok[7:])}\n")
                    break
        f.write(f"sims\t{','.join(sims)}\nreps\t{a.reps}\ncpus_count\t{len(expand_cpus(a.cpus))}\n")
        f.write(f"mem_cap\t{a.mem}\ntimeout\t{a.timeout}\n")
    with open(out / "host.txt", "w") as f:
        f.write("# machine information: keep out of git\n")
        f.write(capture(["lscpu"]) + "\n\n" + capture(["free", "-g"]) + "\n\n" + capture(["uname", "-r"]) + "\n")

    rows = []
    rawf = open(out / "raw.tsv", "a")
    if rawf.tell() == 0:
        rawf.write("\t".join(["bench", "run", "sim", "rep", "exit"] + METRICS + ["verdict"]) + "\n")
    for bench, run, drv, args in runs:
        for rep in range(1, a.reps + 1):
            for sim in sims:
                tag = f"{bench}.{run}.{sim}.r{rep}"
                work = out / "work" / bench / run / sim / f"r{rep}"
                work.mkdir(parents=True, exist_ok=True)
                argv = shlex.split(os.path.expandvars(
                    args.format(sim=sim, work=str(work), dir=str(runfile.parent))))
                log, tfile = out / "logs" / f"{tag}.log", out / "logs" / f"{tag}.time"
                cmd = ["systemd-run", "--user", "--scope", "--quiet", "-p", f"MemoryMax={a.mem}",
                       "-p", "MemorySwapMax=0", "--", "timeout", a.timeout,
                       "taskset", "-c", a.cpus, "nice", "-n", "19", "ionice", "-c3",
                       "/usr/bin/time", "-v", "-o", str(tfile), os.path.expandvars(drv)] + argv
                print(f"[{time.strftime('%H:%M:%S')}] {tag}: {' '.join(shlex.quote(c) for c in cmd[-len(argv)-1:])}",
                      flush=True)
                if a.dry_run:
                    continue
                load1 = os.getloadavg()[0]
                with open(log, "w") as lf:
                    rc = subprocess.run(cmd, stdout=lf, stderr=subprocess.STDOUT,
                                        env=dict(os.environ, TMPDIR=tmp) if tmp else None).returncode
                m = parse_log(log.read_text(errors="replace"))
                wall, rss = parse_time(tfile.read_text(errors="replace")) if tfile.exists() else (None, None)
                m["wall_s"], m["rss_mb"] = wall, rss
                row = dict(bench=bench, run=run, sim=sim, rep=rep, exit=rc, **m)
                rows.append(row)
                rawf.write("\t".join([bench, run, sim, str(rep), str(rc)]
                                     + [fmt(m[k], k) for k in METRICS] + [m["verdict"]]) + "\n")
                rawf.flush()
                print(f"    exit {rc}, {fmt(wall, 'wall_s')} s, {fmt(rss, 'rss_mb')} MB, "
                      f"{m['verdict']} (load average before: {load1:.1f})", flush=True)
                # the numbers are read: drop the run's SAT problems and proofs (GBs per run)
                if not a.keep_work:
                    shutil.rmtree(work, ignore_errors=True)
    rawf.close()
    if a.dry_run:
        return

    summarize(out, rows, a.reps, len(expand_cpus(a.cpus)), a.mem)


def verdict_kind(v: str) -> str:
    """The verdict without its explanation: `COUNTEREXAMPLE (base, replayed)`, not the cycle it
    names (two routes give different CNFs, so the solver may return different counterexamples)."""
    return v.split(" -- ")[0].split(":")[0].strip()


def summarize(out, rows, reps, ncpu, mem):
    # medians per (bench, run, sim)
    keys = []
    for r in rows:
        k = (r["bench"], r["run"], r["sim"])
        if k not in keys:
            keys.append(k)
    summ = []
    for k in keys:
        rs = [r for r in rows if (r["bench"], r["run"], r["sim"]) == k]
        med = {}
        for mname in METRICS:
            vals = [r[mname] for r in rs if r[mname] is not None]
            med[mname] = statistics.median(vals) if vals else None
        verdicts = sorted({verdict_kind(r["verdict"]) for r in rs})
        summ.append(dict(bench=k[0], run=k[1], sim=k[2], n=len(rs),
                         exits=",".join(str(r["exit"]) for r in rs), verdict=" | ".join(verdicts), **med))
    with open(out / "summary.tsv", "w") as f:
        f.write("\t".join(["bench", "run", "sim", "reps", "exits"] + METRICS + ["verdict"]) + "\n")
        for s in summ:
            f.write("\t".join([s["bench"], s["run"], s["sim"], str(s["n"]), s["exits"]]
                              + [fmt(s[m], m) for m in METRICS] + [s["verdict"]]) + "\n")
    head = ["bench", "run", "sim", "setup ms", "build ms", "clauses", "blast ms", "cadical ms",
            "LRAT ms", "wall s", "peak RSS MB", "verdict"]
    with open(out / "summary.md", "w") as f:
        f.write(f"Median of {reps} repetitions per route; cadical and blast are sums over "
                f"obligations; {ncpu} pinned CPUs, memory cap {mem}.\n\n")
        f.write("| " + " | ".join(head) + " |\n|" + "---|" * len(head) + "\n")
        for (b, r) in dict.fromkeys((s["bench"], s["run"]) for s in summ):
            group = [s for s in summ if (s["bench"], s["run"]) == (b, r)]
            differ = len({s["verdict"] for s in group}) > 1
            for s in group:
                v = s["verdict"] + (" **VERDICTS DIFFER**" if differ else "")
                f.write("| " + " | ".join([b, r, s["sim"]] + [fmt(s[m], m) for m in METRICS] + [v]) + " |\n")
    print(f"summary: {out / 'summary.md'}")


def read_raw(path):
    """raw.tsv back into rows (for --summarize)."""
    rows = []
    lines = open(path).read().splitlines()
    head = lines[0].split("\t")
    for line in lines[1:]:
        c = dict(zip(head, line.split("\t")))
        r = dict(bench=c["bench"], run=c["run"], sim=c["sim"], rep=int(c["rep"]), exit=int(c["exit"]),
                 verdict=c["verdict"])
        for m in METRICS:
            r[m] = None if c[m] == "-" else float(c[m])
        rows.append(r)
    return rows


def expand_cpus(spec: str) -> list:
    cpus = []
    for part in spec.split(","):
        if "-" in part:
            lo, hi = part.split("-")
            cpus += list(range(int(lo), int(hi) + 1))
        elif part:
            cpus.append(int(part))
    return cpus


if __name__ == "__main__":
    main()
