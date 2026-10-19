#!/usr/bin/env python3
"""compile.cprop.engine A/B sweep: classic vs aegraph vs aegraph+classic.

The numbers table of todo/livehd/2a-aegraph.html (milestone C). For every
fixture of an in-repo corpus it compiles the design once per engine and records,
per engine: wall ms, peak RSS, nodes in/out, nodes per op after the full
schedule, the nodes the engine created, e-graph and LGraph bytes (the
`cprop-summary` records both engines emit under `lhd.stats`), and, with the
optional legs, LEC A==B and B==source, determinism, synthesis QoR and simulation
digests. The aegraph+classic engine gives the PARITY GAP: what classic still
removes after the e-graph, per fixture and per op. That list drives the order in
which rules are ported. A script is justified because the output is a table of
numbers, not a verdict.

USAGE (from the livehd repo; build once first: bazel build -c opt //lhd:lhd)

  # the default table: compile every inou/prp/tests/equiv base fixture, 3 engines
  scripts/cprop_engine_sweep.py -j 8

  # the quick smoke: 20 fixtures, plus LEC A==B and B==source and determinism
  scripts/cprop_engine_sweep.py --limit 20 --lec --determinism

  # synthesis QoR (gates/area/delay/pred_aig) with the in-repo test liberty
  scripts/cprop_engine_sweep.py --filter 'adder|mux' --synth

  # simulation: out_digest equality + instructions + cycles/s, per engine
  scripts/cprop_engine_sweep.py --corpus sim --sim --engines classic,aegraph

  # the width-agnostic axis: cross the engine with bitfuzz (the set rides every design-under-test command)
  scripts/cprop_engine_sweep.py --set compile.bitfuzz.mode=wires --lec

  # gate G1, width-agnostic leg: aegraph with compile.bitfuzz.mode=wires is LEC-equal to aegraph without it
  scripts/cprop_engine_sweep.py --engines classic,aegraph --width-agnostic

  # the sibling lhdtrack designs (manual): tests/<name>/pyrope/<top>.prp, top from design.toml; the in-repo test.lib
  scripts/cprop_engine_sweep.py --corpus lhdtrack:../lhdtrack --synth --engines classic,aegraph -j 8

  # the extraction cost profile of the aegraph legs (default synth for --synth, sim for --sim)
  scripts/cprop_engine_sweep.py --corpus sim --sim --engines classic,aegraph --sim-cost sim

  # machine-readable output next to the table; any directory is a corpus
  scripts/cprop_engine_sweep.py --corpus path:/some/dir --json sweep.json

SIBLING BENCHMARK REPOS (manual, never in //...): the same engine selector, simulation included

  ../lhdtrack (flows/*.py; run from the lhdtrack checkout with its usual `lhdtrack run ...`)
    synthesis A/B      LHDTRACK_EXTRA_FLOWS=syn_lhd_pyrope_aegraph  lhdtrack run ...
                       (flows/syn_lhd_pyrope_aegraph.py: syn_lhd_pyrope + --set compile.cprop.engine=aegraph;
                        LHDTRACK_CPROP_ENGINE=aegraph+classic picks the third engine)
    simulation A/B     LHDTRACK_EXTRA_FLOWS=sim_lhd_pyrope_aegraph_llvm  lhdtrack run ...
                       (flows/sim_lhd_pyrope_aegraph_llvm.py; the baseline row is sim_lhd_pyrope_llvm; both are
                        gated by the testbench checksum, so a miscompile cannot show as a speedup; read
                        exec_ms, cycles_per_s and cc_ms next to sim.cprop_engine in the result)
    any sim flow       LHDTRACK_CPROP_ENGINE=aegraph lhdtrack run ...          (every sim_lhd_* flow, both lhd sim legs)
    free-form sets     LHDTRACK_SIM_SET="compile.cprop.engine=aegraph compile.cprop.engine_slots=first"  (sim)
                       LHDTRACK_LHD_SET="compile.cprop.engine=aegraph"                                   (synth/lec)
                       COST PROFILE for a simulation row: add compile.aegraph.cost=sim (or =auto, which is sim under `lhd sim`) to
                       LHDTRACK_SIM_SET / BENCH_PYROPE_SETS / CORE_SIM_SETS; the default profile is synth for every command
    LEC of the netlist is the existing lec_netlist flow: it proves the engine's mapped design against the
    source, so run it with the same LHDTRACK_EXTRA_FLOWS to get the B==source leg for the synthesis row.

  ../lhdsuite (bench/*.sh; bazel targets are //bench:<core>_<scenario>, e.g. minion_sim_pyrope, minion_synth)
    BENCH_PYROPE_SETS rides EVERY Pyrope front-end command of a scenario: compile, synth, sim (both lhd sim
    legs and the timed rerun), and the impl side of lec. It is the engine selector:
        bazel test //bench:minion_sim_pyrope --override_module=livehd=../livehd \
            --test_env=BENCH_PYROPE_SETS="compile.cprop.engine=aegraph"
        bazel test //bench:minion --override_module=livehd=../livehd \
            --test_env=BENCH_PYROPE_SETS="compile.cprop.engine=aegraph+classic"
      cores with simulation targets: minion, dino, xiangshan (xs_*), matched_filter, cva6, rocket.
      Run the same target with and without the --test_env for the A/B pair. bench/ledger.py does NOT
      record BENCH_PYROPE_SETS in a row's identity: keep the two runs apart yourself (copy
      bazel-testlogs/bench/<target>/ between runs, or bazel run //bench:show right after each).
    CORE_SIM_SETS ("--set k=v" tokens, `lhd sim` only) is set per core by defs.bzl (sim_sets) and therefore
    cannot be overridden from --test_env; when driving bench/sim.sh by hand, append the engine there:
        CORE_SIM_SETS="--set compile.cprop.engine=aegraph" bench/sim.sh      (inside the scenario environment)
    or edit the core's sim_sets in bench/defs.bzl for a permanent per-core pin.
  ../logikbench (mux and arith classes): pass `--set compile.cprop.engine=aegraph` to its lhd compile/synth.

ENVIRONMENT
  LHD               the lhd binary (default <repo>/bazel-bin/lhd/lhd). ./bazel-bin flips
                    to whichever -c config was built LAST, so pin one for a long sweep.
  CPROP_SWEEP_KEEP  =1 keeps every workdir under <out>/ (default: removed on success).

HOW IT MEASURES (the A/B contract of the design doc)
  * every compile runs cold: --set lhd.incremental=false, a fresh --workdir.
  * the engine knob rides ONLY the design under test. LEC A==B compares the engine's
    lg: against the classic lg:; LEC B==source compares it against the .prp itself,
    and never passes the engine knob, so the reference does not go through the aegraph.
  * `ms` is the process wall time; `cprop ms` is the sum of the per-graph cprop-summary
    ms (both compile cprop slots). Both engines use min(graphs, cores, 16) workers, so
    the per-graph sum and the wall time are different numbers: report both.
  * RSS is the peak resident size of the compile process (wait4 ru_maxrss).
  * the GATE G2 table (printed under "gate G2") counts the nodes of the lg the full schedule
    leaves (`lhd tool cat lg:DIR --target node`, fields final_nodes / final_by_op), and with --synth
    the pred_aig of the mapped design: geomean and worst fixture of aegraph/classic, the op classes
    more than 5% above classic, and the failures. The cprop-summary numbers are mid-pipeline
    (bitwidth and the emitters run after the last slot).
  * the GATE G3 table (printed under "gate G3 (synthesis)" with --synth): per fixture aegraph/classic of the ABC gates, area,
    max_delay and pred_aig (qor.abc.total of `lhd synth` with the in-repo test.lib), geomean / worst / better / worse / sums.
    Corpora: equiv|sim|lgraph|path:DIR, and lhdtrack:DIR for the sibling ../lhdtrack designs (tests/<name>/pyrope/<top>.prp,
    top from design.toml). `--synth-cost` / `--sim-cost` pick compile.aegraph.cost of the legs (default synth / sim).
  * `--sim` records `static instr`: the machine instructions of the compiled design (objdump of the
    `sim/*.llvm.o` color kernels and `sim/obj/*.o`, minus the driver and tuning stubs). A color kernel is
    straight-line code run once per cycle, so this is the per-cycle budget; it stands in for the perf
    `instructions` counter, which reads 0 on hosts with perf_event_paranoid > 1.
  * `nodes by op` is the by_op of the LAST cprop slot (after enableopt), the schedule's
    final node mix. Classic's low_lane/opshare/mux_share ADD nodes on purpose, so node
    count alone is misleading: read the per-op table next to it.
  * `parity gap` (engine aegraph+classic): per cprop slot, the nodes the classic step
    still removed from what the aegraph step left (nodes_in - nodes_out of the pass.cprop
    record), and per op the by_op difference between the aegraph record and the classic
    record that follows it.

The lec legs report the verdict word of `lhd lec --result-json` (proven-full, tested, timeout, ...);
exit codes 10 (REFUTED with a counterexample) and 7 (REFUTED otherwise) are reported as REFUTED.
A timeout is NOT a proof: read the verdict column, not only the absence of REFUTED.
"""
import argparse
import concurrent.futures as cf
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LHD = os.environ.get("LHD", os.path.join(REPO, "bazel-bin", "lhd", "lhd"))
ENGINES_ALL = ("classic", "aegraph", "aegraph+classic")
TEST_LIB = os.path.join(REPO, "inou", "prp", "tests", "abc", "test.lib")
CORPORA = {
    "equiv": os.path.join(REPO, "inou", "prp", "tests", "equiv"),
    "sim": os.path.join(REPO, "inou", "prp", "tests", "sim"),
    "lgraph": os.path.join(REPO, "inou", "prp", "tests", "lgraph"),
}
HEADER_RE = re.compile(r"^:(\w+):\s*(.*?)\s*$")


# ---- process helpers ---------------------------------------------------------


def run_timed(cmd, cwd, timeout, env=None):
    """(rc, combined output, wall ms, peak rss KiB). rc 124 = timeout."""
    start = time.monotonic()
    out_path = os.path.join(cwd, f".out.{os.getpid()}.{time.monotonic_ns()}")
    with open(out_path, "w") as sink:
        p = subprocess.Popen(cmd, cwd=cwd, stdout=sink, stderr=subprocess.STDOUT, env=env)
        deadline = start + timeout
        status = None
        rss = 0
        while True:
            pid, st, usage = os.wait4(p.pid, os.WNOHANG)
            if pid:
                status, rss = st, usage.ru_maxrss
                break
            if time.monotonic() > deadline:
                p.kill()
                _, _, usage = os.wait4(p.pid, 0)
                status, rss = None, usage.ru_maxrss
                break
            time.sleep(0.01)
    ms = (time.monotonic() - start) * 1000.0
    try:
        with open(out_path, errors="replace") as f:
            out = f.read()
        os.unlink(out_path)
    except OSError:
        out = ""
    if status is None:
        return 124, out + "TIMEOUT", ms, rss
    rc = os.waitstatus_to_exitcode(status)
    return rc if rc >= 0 else 128 - rc, out, ms, rss


def last_line(out, width=200):
    lines = [line for line in out.strip().splitlines() if line.strip()]
    return lines[-1][:width] if lines else ""


# ---- corpus ------------------------------------------------------------------


def read_header(path):
    tags = {}
    sets = []
    try:
        with open(path) as f:
            text = f.read(4096)
    except OSError:
        return tags, sets
    if not text.lstrip().startswith("/*"):
        return tags, sets
    body = text.split("*/", 1)[0]
    for line in body.splitlines():
        m = HEADER_RE.match(line.strip())
        if not m:
            continue
        if m.group(1) == "set":
            sets.extend(m.group(2).split())
        else:
            tags.setdefault(m.group(1), m.group(2))
    return tags, sets


def design_toml_top(prp):
    """The `top` of the lhdtrack test a pyrope file belongs to (tests/<name>/design.toml), or ""."""
    toml = os.path.join(os.path.dirname(os.path.dirname(prp)), "design.toml")
    try:
        with open(toml) as f:
            for line in f:
                m = re.match(r'^top\s*=\s*"([^"]+)"', line)
                if m:
                    return m.group(1)
    except OSError:
        pass
    return ""


def declared_stem_top(prp):
    """The file stem when the file declares a module of that name (`mod stem`, `comb stem`): the top of the tests/abc and
    tests/pyrope abc_* fixtures, which carry no :top: tag."""
    stem = os.path.basename(prp)[:-4]
    try:
        with open(prp) as f:
            text = f.read()
    except OSError:
        return ""
    return stem if re.search(r"^\s*(pub\s+)?(mod|comb|pipe|flow)\s+" + re.escape(stem) + r"\b", text, re.M) else ""


def corpus_files(corpus):
    if corpus.startswith("lhdtrack:"):
        # ../lhdtrack (a sibling repo, manual): every tests/<name>/pyrope/<top>.prp, top from design.toml
        root = os.path.join(corpus[9:], "tests")
        out = []
        for name in sorted(os.listdir(root)) if os.path.isdir(root) else []:
            toml = os.path.join(root, name, "design.toml")
            prp_dir = os.path.join(root, name, "pyrope")
            if not os.path.isfile(toml) or not os.path.isdir(prp_dir):
                continue
            top = design_toml_top(os.path.join(prp_dir, "x.prp"))
            prp = os.path.join(prp_dir, top + ".prp")
            if top and os.path.isfile(prp):
                out.append(prp)
        return out
    root = CORPORA.get(corpus) or (os.path.abspath(corpus[5:]) if corpus.startswith("path:") else None)
    if root is None or not os.path.isdir(root):
        sys.exit(f"unknown corpus '{corpus}' (equiv|sim|lgraph|path:DIR|lhdtrack:DIR)")
    names = sorted(f for f in os.listdir(root) if f.endswith(".prp"))
    stems = {n[:-4] for n in names}
    out = []
    for n in names:
        stem = n[:-4]
        # `foo_1.prp` next to `foo.prp` is a header-only VARIANT of foo: not a fixture.
        m = re.match(r"^(.*)_\d+$", stem)
        if m and m.group(1) in stems:
            continue
        out.append(os.path.join(root, n))
    return out


# ---- diagnostics ---------------------------------------------------------------


def read_summaries(diag_path):
    """The cprop-summary records, in emission order."""
    records = []
    try:
        with open(diag_path, errors="replace") as f:
            for line in f:
                line = line.strip()
                if not line.startswith("{"):
                    continue
                try:
                    d = json.loads(line)
                except ValueError:
                    continue
                if d.get("code") == "cprop-summary":
                    a = d.get("attrs", {})
                    records.append(
                        {
                            "pass": d.get("pass", ""),
                            "graph": a.get("graph", ""),
                            "nodes_in": int(a.get("nodes_in", 0)),
                            "nodes_out": int(a.get("nodes_out", 0)),
                            "by_op": parse_by_op(a.get("by_op", "")),
                            "ms": float(a.get("ms", 0)),
                            "rules_fired": int(a.get("rules_fired", 0)),
                            "creates": int(a.get("creates", 0)),
                            "aeg_bytes": int(a.get("aeg_bytes", 0)),
                            "lg_bytes": int(a.get("lg_bytes", 0)),
                            "aeg_cap_bytes": int(a.get("aeg_cap_bytes", 0)),
                            "lg_bytes_v1": int(a.get("lg_bytes_v1", 0)),
                        }
                    )
    except OSError:
        pass
    return records


def parse_by_op(text):
    ops = {}
    for item in text.split(","):
        if "=" in item:
            k, v = item.rsplit("=", 1)
            ops[k] = int(v)
    return ops


def add_ops(into, ops, sign=1):
    for k, v in ops.items():
        into[k] = into.get(k, 0) + sign * v


def read_rule_census(diag_path):
    """The aegraph-rule records of one compile (a pass.aegraph step emits one per rule that ran),
    summed over the steps: {rule: {tried, matched, applied, subsumed, limit_rejected, extracted}}."""
    census = {}
    try:
        with open(diag_path, errors="replace") as f:
            for line in f:
                if '"aegraph-rule"' not in line:
                    continue
                try:
                    d = json.loads(line)
                except ValueError:
                    continue
                a = d.get("attrs", {})
                r = census.setdefault(a.get("rule", "?"), {"group": a.get("group", ""), "tag": a.get("tag", "")})
                for k in ("tried", "matched", "applied", "subsumed", "limit_rejected", "extracted"):
                    r[k] = r.get(k, 0) + int(a.get(k, 0))
    except OSError:
        pass
    return census


def summarize(records, engine):
    """Fold the per-graph, per-slot records of ONE compile into one row."""
    by_graph = {}
    for r in records:
        by_graph.setdefault(r["graph"], []).append(r)
    s = {
        "graphs": len(by_graph),
        "cprop_ms": sum(r["ms"] for r in records),
        "creates": sum(r["creates"] for r in records),
        "aeg_bytes": max((r["aeg_bytes"] for r in records), default=0),
        "lg_bytes": 0,
        "aeg_cap_bytes": max((r["aeg_cap_bytes"] for r in records), default=0),
        "g1": [],  # per graph (aeg, aeg_cap, lg, lg_v1) bytes, for the gate-G1 ratios
        "nodes_in": 0,
        "nodes_out": 0,
        "by_op": {},
        "gap_nodes": 0,
        "gap_by_op": {},
    }
    lg_peak = 0
    for graph, rs in by_graph.items():
        s["nodes_in"] += rs[0]["nodes_in"]
        s["nodes_out"] += rs[-1]["nodes_out"]
        add_ops(s["by_op"], rs[-1]["by_op"])
        lg_peak += max(r["lg_bytes"] for r in rs)
        s["g1"].append((max(r["aeg_bytes"] for r in rs), max(r["aeg_cap_bytes"] for r in rs),
                        max(r["lg_bytes"] for r in rs), max(r["lg_bytes_v1"] for r in rs)))
        if engine == "aegraph+classic":
            # records alternate aegraph, classic per slot: the classic record's input
            # is what the aegraph left; its by_op difference is what classic removed.
            for a, c in zip(rs[0::2], rs[1::2]):
                if a["pass"] == "pass.aegraph" and c["pass"] == "pass.cprop":
                    s["gap_nodes"] += c["nodes_in"] - c["nodes_out"]
                    add_ops(s["gap_by_op"], a["by_op"])
                    add_ops(s["gap_by_op"], c["by_op"], -1)
    s["lg_bytes"] = lg_peak
    return s


# ---- one fixture -------------------------------------------------------------------


def lhd_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


def one(fixture, args, workroot):
    name = os.path.basename(fixture)[:-4]
    wd = os.path.abspath(os.path.join(workroot, re.sub(r"\W+", "_", name)))
    shutil.rmtree(wd, ignore_errors=True)
    os.makedirs(wd, exist_ok=True)
    tags, sets = read_header(fixture)
    top = tags.get("pyrope_top") or tags.get("lec_top") or tags.get("top") or design_toml_top(fixture) or declared_stem_top(fixture)
    base_sets = [x for s in sets + list(args.extra_sets) if not s.startswith("compile.cprop.engine") for x in ("--set", s)]
    top_args = ["--top", top] if top else []
    res = {"fixture": name, "top": top, "status": "ok", "detail": "", "engines": {}}

    def compile_cmd(engine, tag, extra=()):
        return (
            [LHD, "compile", fixture, *top_args, "--workdir", f"{wd}/w_{tag}", "--stats", "--set", "lhd.incremental=false"]
            + base_sets
            + ["--set", f"compile.cprop.engine={engine}", "--emit-dir", f"lg:{wd}/lg_{tag}", "--emit", f"diagnostics:{wd}/diag_{tag}.jsonl"]
            + list(extra)
        )

    for engine in args.engines:
        rc, out, ms, rss = run_timed(compile_cmd(engine, engine), wd, args.timeout)
        row = {"rc": rc, "ms": ms, "rss_mb": rss / 1024.0}
        if rc != 0:
            row["detail"] = last_line(out)
        else:
            row.update(summarize(read_summaries(f"{wd}/diag_{engine}.jsonl"), engine))
            if engine != "classic":
                row["rules"] = read_rule_census(f"{wd}/diag_{engine}.jsonl")
            row["final_nodes"], row["final_by_op"] = final_nodes(wd, engine, args.timeout)
        res["engines"][engine] = row

    classic = res["engines"].get("classic")
    if classic is not None and classic["rc"] != 0:
        res["status"] = "BASE_COMPILE_FAIL"
        res["detail"] = classic.get("detail", "")
        return finish(res, wd)

    for engine in args.engines:
        row = res["engines"][engine]
        if row["rc"] != 0:
            res["status"] = "ENGINE_COMPILE_FAIL"
            res["detail"] = f"{engine}: {row.get('detail', '')}"

    if args.determinism and "aegraph" in args.engines and res["engines"]["aegraph"]["rc"] == 0:
        rc, out, _, _ = run_timed(compile_cmd("aegraph", "aegraph2"), wd, args.timeout)
        if rc != 0:
            res["engines"]["aegraph"]["determinism"] = "COMPILE_FAIL"
        else:
            rc, out, _, _ = run_timed([LHD, "tool", "diff", f"lg:{wd}/lg_aegraph", f"lg:{wd}/lg_aegraph2", "--structural"], wd, args.timeout)
            res["engines"]["aegraph"]["determinism"] = "same" if rc == 0 else "DIFFERENT"

    if args.lec and classic is not None and top:
        lec_top = top.split(".")[-1] if top else ""
        for engine in args.engines:
            row = res["engines"][engine]
            if row["rc"] != 0:
                continue
            tail = ["--set", "formal.effort=proof", "--set", f"formal.timeout={args.lec_timeout}", "--set", "lhd.incremental=false"]
            top_lec = ["--top", top or lec_top] if (top or lec_top) else []
            if engine != "classic":
                row["lec_ab"] = lec_leg(wd, f"ab_{engine}", f"lg:{wd}/lg_{engine}", f"lg:{wd}/lg_classic", top_lec, tail, args)
            row["lec_src"] = lec_leg(wd, f"src_{engine}", f"lg:{wd}/lg_{engine}", fixture, top_lec, tail, args)

    if args.width_agnostic and top and "aegraph" in args.engines and res["engines"]["aegraph"]["rc"] == 0:
        # G1: the aegraph result with compile.bitfuzz.mode=wires is LEC-equal to the result without it.
        rc, out, _, _ = run_timed(compile_cmd("aegraph", "aegraph_wires", ["--set", "compile.bitfuzz.mode=wires"]), wd, args.timeout)
        if rc != 0:
            res["engines"]["aegraph"]["lec_wa"] = "ERR compile: " + last_line(out)
        else:
            tail = ["--set", "formal.effort=proof", "--set", f"formal.timeout={args.lec_timeout}", "--set", "lhd.incremental=false"]
            res["engines"]["aegraph"]["lec_wa"] = lec_leg(wd, "wa", f"lg:{wd}/lg_aegraph_wires", f"lg:{wd}/lg_aegraph", ["--top", top], tail, args)

    if args.synth:
        for engine in args.engines:
            if res["engines"][engine]["rc"] != 0:
                continue
            rj = f"{wd}/synth_{engine}.json"
            cmd = (
                [LHD, "synth", fixture, *top_args, "--workdir", f"{wd}/sw_{engine}", "--set", "lhd.incremental=false", "--set", f"synth.liberty={TEST_LIB}", "--set", "synth.opentimer=false", "--set", f"compile.cprop.engine={engine}", "--result-json", rj]
                + cost_sets(args.synth_cost, base_sets)
                + base_sets
            )
            rc, out, ms, _ = run_timed(cmd, wd, args.timeout)
            row = res["engines"][engine]
            if rc != 0:
                row["synth"] = {"rc": rc}
                continue
            total = lhd_json(rj).get("qor", {}).get("abc", {}).get("total", {})
            row["synth"] = {k: total.get(k) for k in ("gates", "area", "max_delay", "pred_aig")}

    if args.sim:
        for engine in args.engines:
            if res["engines"][engine]["rc"] != 0:
                continue
            rj = f"{wd}/sim_{engine}.json"
            cmd = [LHD, "sim", fixture, "--workdir", f"{wd}/simw_{engine}", "--set", "sim.tune.profile=off", "--set", "sim.ninja=false", "--set", "lhd.incremental=false", "--set", "lhd.jobs=2", "--set", f"compile.cprop.engine={engine}", "--result-json", rj, "-q"] + cost_sets(args.sim_cost, base_sets) + base_sets
            rc, out, ms, _ = run_timed(cmd, wd, args.timeout)
            row = res["engines"][engine]
            if rc != 0:
                row["sim"] = {"rc": rc, "detail": last_line(out)}
                continue
            tests = lhd_json(rj).get("tests", [])
            static, kernel = static_instructions(f"{wd}/simw_{engine}")
            row["sim"] = {
                "static_instr": static,
                "kernel_instr": kernel,
                "tests": len(tests),
                "pass": sum(1 for t in tests if t.get("status") == "pass"),
                "instructions": sum(t.get("instructions", 0) for t in tests),
                "cycles": sum(t.get("sim_cycles", 0) for t in tests),
                "sim_ns": sum(t.get("sim_ns", 0) for t in tests),
                "digest": ",".join(f"{t.get('test')}:{t.get('out_digest')}" for t in tests),
            }
    return finish(res, wd)


def final_nodes(wd, engine, timeout):
    """The nodes of the lg: the full schedule left (`lhd tool cat lg:DIR --target node`): (total, {op: count}).
    The cprop-summary of the last slot is a mid-pipeline number (bitwidth and the emitters still run after it)."""
    rc, out, _, _ = run_timed([LHD, "tool", "cat", f"lg:{wd}/lg_{engine}", "--target", "node", "--max", "0"], wd, timeout)
    by_op = {}
    if rc == 0:
        for line in out.splitlines():
            if not line.startswith("{"):
                continue
            try:
                d = json.loads(line)
            except ValueError:
                continue
            if d.get("t") == "node":
                by_op[d.get("kind", "?")] = by_op.get(d.get("kind", "?"), 0) + 1
    return sum(by_op.values()), by_op


def static_instructions(workdir):
    """Machine instructions of the compiled design in a `lhd sim` workdir: the LLVM color kernels
    (`sim/*.llvm.o`) plus the per-module objects (`sim/obj/*.o`, minus the driver and the tuning stubs).
    A color kernel is straight-line code that runs once per cycle, so this is the per-cycle instruction
    budget -- the deterministic stand-in for the `instructions` perf counter, which hosts with
    perf_event_paranoid > 1 do not give (the `instructions` column then reads 0)."""
    objs = []
    for root, _, files in os.walk(os.path.join(workdir, "sim")):
        for f in files:
            if f.endswith(".o") and not f.startswith("drv") and ".tune" not in f:
                objs.append(os.path.join(root, f))
    total = kernel = 0
    for o in sorted(objs):
        try:
            out = subprocess.run(["objdump", "-d", "--no-show-raw-insn", o], capture_output=True, text=True, timeout=120).stdout
        except (OSError, subprocess.TimeoutExpired):
            return None, None
        n = sum(1 for line in out.splitlines() if re.match(r"^\s+[0-9a-f]+:\t", line))
        total += n
        kernel += n if o.endswith(".llvm.o") else 0
    return total, kernel


def lec_leg(wd, tag, impl, ref, top_lec, tail, args):
    """One `lhd lec`: the verdict word (proven-full, ..., REFUTED) from --result-json, else the exit code."""
    rj = f"{wd}/lec_{tag}.json"
    cmd = [LHD, "lec", "--impl", impl, "--ref", ref, *top_lec, *tail, "--workdir", f"{wd}/lec_{tag}", "--result-json", rj]
    rc, out, _, _ = run_timed(cmd, wd, args.lec_timeout + 60)
    if rc in (7, 10):
        return "REFUTED"
    verdict = lhd_json(rj).get("lec", {}).get("verdict")
    if rc == 0:
        return verdict or "ok"
    return f"ERR{rc}"


def finish(res, wd):
    if os.environ.get("CPROP_SWEEP_KEEP") != "1":
        shutil.rmtree(wd, ignore_errors=True)
    return res


# ---- tables ------------------------------------------------------------------------------


def fmt(x, spec="{:.0f}"):
    return "-" if x is None else spec.format(x)


def cost_sets(profile, base_sets):
    """--set compile.aegraph.cost=PROFILE for the leg, unless the caller passed their own --set for it."""
    if not profile or any("compile.aegraph.cost=" in str(x) for x in base_sets):
        return []
    return ["--set", f"compile.aegraph.cost={profile}"]


def geomean(xs):
    xs = [x for x in xs if x and x > 0]
    return statistics.geometric_mean(xs) if xs else None


def md_table(header, rows):
    out = ["| " + " | ".join(header) + " |", "|" + "|".join(["---"] * len(header)) + "|"]
    out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
    return "\n".join(out)


def report(results, args):
    engines = args.engines
    ok = [r for r in results if r["status"] in ("ok", "ENGINE_COMPILE_FAIL")]
    print(f"# cprop engine sweep: corpus={args.corpus} fixtures={len(results)} engines={','.join(engines)}\n")
    status = {}
    for r in results:
        status[r["status"]] = status.get(r["status"], 0) + 1
    print("status: " + ", ".join(f"{k}={v}" for k, v in sorted(status.items())) + "\n")

    # per-engine totals over the fixtures every engine compiled
    common = [r for r in ok if all(r["engines"][e]["rc"] == 0 for e in engines)]
    rows = []
    for e in engines:
        rs = [r["engines"][e] for r in common]
        rows.append([
            e,
            len(rs),
            fmt(sum(x["ms"] for x in rs)),
            fmt(sum(x["cprop_ms"] for x in rs), "{:.1f}"),
            fmt(max((x["rss_mb"] for x in rs), default=0)),
            sum(x["nodes_in"] for x in rs),
            sum(x["nodes_out"] for x in rs),
            sum(x["creates"] for x in rs),
            fmt(max((x["aeg_bytes"] for x in rs), default=0) / 1024.0),
            fmt(sum(x["lg_bytes"] for x in rs) / 1024.0),
        ])
    print("## totals over the fixtures all engines compiled\n")
    print(md_table(["engine", "fixtures", "wall ms", "cprop ms (sum of graphs)", "peak RSS MB", "nodes in", "nodes out", "creates", "peak aeg KiB", "lg KiB"], rows))
    print()

    # Gate G1 memory: aeg/lg per graph under BOTH aeg measures (in use, capacity) and BOTH lg
    # yardsticks (v2: nodes*64+edges*48 at the largest; v1: nodes*32+edges*16 at the output).
    g1 = [g for e in engines if e != "classic" for r in common for g in r["engines"][e].get("g1", [])]
    g1 = [g for g in g1 if g[2] >= 50 * 1024]
    if g1:
        def pct(vals, q):
            vals = sorted(vals)
            return vals[min(len(vals) - 1, int(q * len(vals)))]
        rows = []
        for label, num, den in (("in-use / lg v2", 0, 2), ("capacity / lg v2", 1, 2), ("in-use / lg v1", 0, 3), ("capacity / lg v1", 1, 3)):
            ratios = [g[num] / g[den] for g in g1 if g[den]]
            if ratios:
                rows.append([label, len(ratios), fmt(pct(ratios, 0.5), "{:.2f}"), fmt(pct(ratios, 0.9), "{:.2f}"), fmt(max(ratios), "{:.2f}"),
                             sum(1 for x in ratios if x > 0.5)])
        print("## gate G1 memory: aeg/lg per graph (graphs with lg v2 of at least 50 KB)\n")
        print(md_table(["ratio", "graphs", "median", "p90", "max", "over 0.5"], rows))
        print()

    # Gate G2 (structural parity): the lg nodes the full schedule leaves, and pred_aig with --synth.
    if "classic" in engines:
        for e in engines:
            if e == "classic":
                continue
            ratios, big, pred, pred_big = [], [], [], []
            for r in common:
                c, a = r["engines"]["classic"], r["engines"][e]
                if c.get("final_nodes") and a.get("final_nodes"):
                    q = a["final_nodes"] / c["final_nodes"]
                    ratios.append(q)
                    if q > 1.10:
                        big.append((q, r["fixture"], c["final_nodes"], a["final_nodes"]))
                cs, as_ = c.get("synth") or {}, a.get("synth") or {}
                if cs.get("pred_aig") and as_.get("pred_aig"):
                    q = as_["pred_aig"] / cs["pred_aig"]
                    pred.append(q)
                    if q > 1.10:
                        pred_big.append((q, r["fixture"], cs["pred_aig"], as_["pred_aig"]))
            if not ratios:
                continue
            per_op = {}
            for r in common:
                for op, n in r["engines"]["classic"].get("final_by_op", {}).items():
                    per_op.setdefault(op, [0, 0])[0] += n
                for op, n in r["engines"][e].get("final_by_op", {}).items():
                    per_op.setdefault(op, [0, 0])[1] += n
            worse = [(op, c, a) for op, (c, a) in per_op.items() if a > c * 1.05 and a - c >= 1]
            failed = sum(1 for r in results if r["status"] != "ok" or r["engines"][e]["rc"] != 0)
            print(f"## gate G2: {e} vs classic (structural parity)\n")
            rows = [["total nodes (lg after the full schedule), geomean", fmt(geomean(ratios), "{:.4f}"), "within 2%: " + ("yes" if geomean(ratios) <= 1.02 else "NO")],
                    ["total nodes, worst fixture", fmt(max(ratios), "{:.3f}"), "no fixture over 10%: " + ("yes" if not big else f"NO ({len(big)})")]]
            if pred:
                rows += [["pred_aig, geomean", fmt(geomean(pred), "{:.4f}"), "within 2%: " + ("yes" if geomean(pred) <= 1.02 else "NO")],
                         ["pred_aig, worst fixture", fmt(max(pred), "{:.3f}"), "no fixture over 10%: " + ("yes" if not pred_big else f"NO ({len(pred_big)})")]]
            rows.append(["op classes more than 5% above classic (summed)", len(worse), "none: " + ("yes" if not worse else "NO")])
            rows.append(["failures", failed, "0: " + ("yes" if not failed else "NO")])
            print(md_table(["measure", "value", "gate"], rows))
            print()
            if worse:
                print(md_table(["op", "classic", e, "ratio"], [[op, c, a, fmt(a / c if c else float("inf"), "{:.3f}")] for op, c, a in sorted(worse, key=lambda t: -t[2] / max(t[1], 1))]))
                print()
            if big or pred_big:
                print(md_table(["fixture", "measure", "classic", e, "ratio"],
                               [[f, "nodes", c, a, fmt(q, "{:.2f}")] for q, f, c, a in sorted(big, reverse=True)[:30]]
                               + [[f, "pred_aig", c, a, fmt(q, "{:.2f}")] for q, f, c, a in sorted(pred_big, reverse=True)[:30]]))
                print()

    # nodes per op
    ops = {}
    for r in common:
        for e in engines:
            for op, n in r["engines"][e]["by_op"].items():
                ops.setdefault(op, {k: 0 for k in engines})[e] += n
    rows = []
    for op, per in sorted(ops.items(), key=lambda kv: -max(kv[1].values())):
        rows.append([op] + [per[e] for e in engines] + ([per["aegraph"] - per["classic"]] if "aegraph" in engines and "classic" in engines else []))
    print("## nodes per op after the full schedule (last cprop slot)\n")
    print(md_table(["op"] + list(engines) + (["aegraph - classic"] if "aegraph" in engines and "classic" in engines else []), rows))
    print()

    if "aegraph+classic" in engines:
        gap_ops = {}
        gap_fix = []
        for r in common:
            row = r["engines"]["aegraph+classic"]
            if row["gap_nodes"]:
                gap_fix.append((row["gap_nodes"], r["fixture"]))
            for op, n in row["gap_by_op"].items():
                gap_ops[op] = gap_ops.get(op, 0) + n
        print("## parity gap: what classic still removes after the aegraph\n")
        total = sum(g for g, _ in gap_fix)
        print(f"total nodes removed by classic after the aegraph: {total} over {len(gap_fix)} of {len(common)} fixtures\n")
        print(md_table(["op", "nodes classic still removes"], [[op, n] for op, n in sorted(gap_ops.items(), key=lambda kv: -kv[1]) if n]))
        print()
        print(md_table(["fixture", "gap nodes"], [[f, g] for g, f in sorted(gap_fix, reverse=True)[:25]]))
        print()

    # the rule census of the aegraph engines (milestone D)
    for engine in ("aegraph", "aegraph+classic"):
        if engine not in engines:
            continue
        census = {}
        for r in common:
            for rule, c in r["engines"][engine].get("rules", {}).items():
                t = census.setdefault(rule, {"group": c["group"], "tag": c["tag"]})
                for k in ("tried", "matched", "applied", "subsumed", "limit_rejected", "extracted"):
                    t[k] = t.get(k, 0) + c.get(k, 0)
        if census:
            print(f"## rule census ({engine}, summed over the fixtures)\n")
            cols = ("tried", "matched", "applied", "subsumed", "limit_rejected", "extracted")
            print(md_table(["rule", "group", "tag", *cols], [[rule, c["group"], c["tag"], *[c[k] for k in cols]] for rule, c in sorted(census.items(), key=lambda kv: -kv[1]["applied"])]))
            print()

    # speed ratio
    if "classic" in engines:
        for e in engines:
            if e == "classic":
                continue
            ratios = [r["engines"][e]["cprop_ms"] / r["engines"]["classic"]["cprop_ms"] for r in common if r["engines"]["classic"]["cprop_ms"] > 0]
            if ratios:
                print(f"cprop ms {e}/classic: geomean {geomean(ratios):.2f}x over {len(ratios)} fixtures\n")

    # optional legs
    if args.lec or args.width_agnostic:
        print("## LEC (verdict word of lhd lec; REFUTED must be 0)\n")
        counts = {}
        bad = []
        for r in ok:
            for e, row in r["engines"].items():
                for leg in ("lec_ab", "lec_src", "lec_wa"):
                    if leg in row:
                        counts[(e, leg, row[leg])] = counts.get((e, leg, row[leg]), 0) + 1
                        if row[leg].startswith(("REFUTED", "ERR")):
                            bad.append([r["fixture"], e, leg, row[leg]])
        print(md_table(["engine", "leg", "verdict", "fixtures"], [[e, leg, v, n] for (e, leg, v), n in sorted(counts.items())]))
        print()
        if bad:
            print(md_table(["fixture", "engine", "leg", "verdict"], bad[:60]))
            print()
    if args.determinism:
        d = [r["engines"]["aegraph"].get("determinism") for r in ok if "aegraph" in r["engines"]]
        print(f"## determinism (aegraph twice, tool diff --structural): same={d.count('same')} different={d.count('DIFFERENT')} compile_fail={d.count('COMPILE_FAIL')}\n")
    if args.synth:
        rows = []
        for r in ok:
            for e in engines:
                s = r["engines"][e].get("synth")
                if s and "rc" not in s:
                    rows.append([r["fixture"], e, s["gates"], s["area"], s["max_delay"], s["pred_aig"]])
        print("## synthesis QoR (qor.abc.total)\n")
        print(md_table(["fixture", "engine", "gates", "area", "max_delay", "pred_aig"], rows[: args.rows]))
        print()
        # gate G3, synthesis half: ABC area and delay of the aegraph against classic, per fixture
        for e in engines:
            if e == "classic":
                continue
            stats = {k: [] for k in ("gates", "area", "max_delay", "pred_aig")}
            worst = {k: [] for k in stats}
            tot = {k: [0.0, 0.0] for k in stats}
            for r in ok:
                a, c = r["engines"].get(e, {}).get("synth"), r["engines"].get("classic", {}).get("synth")
                if not a or not c or "rc" in a or "rc" in c:
                    continue
                for k in stats:
                    if a.get(k) and c.get(k):
                        q = a[k] / c[k]
                        stats[k].append(q)
                        worst[k].append((q, r["fixture"], c[k], a[k]))
                        tot[k][0] += c[k]
                        tot[k][1] += a[k]
            if not stats["area"]:
                continue
            print(f"## gate G3 (synthesis): {e} (cost={args.synth_cost}) vs classic, ABC with the in-repo test.lib\n")
            grows = []
            for k in stats:
                xs = stats[k]
                grows.append([k, len(xs), fmt(geomean(xs), "{:.4f}"), fmt(max(xs), "{:.3f}"), sum(1 for q in xs if q < 1.0), sum(1 for q in xs if q > 1.0),
                              f"{tot[k][1]:.0f} / {tot[k][0]:.0f} = {tot[k][1] / tot[k][0]:.4f}"])
            print(md_table(["measure", "fixtures", "geomean aegraph/classic", "worst", "better", "worse", "sum aegraph / classic"], grows))
            ga, gd = geomean(stats["area"]), geomean(stats["max_delay"])
            print(f"\nG3 area geomean <= 1: {'yes' if ga <= 1.0 else 'NO'}; delay geomean <= 1: {'yes' if gd is not None and gd <= 1.0 else 'NO'}\n")
            for k in ("area", "max_delay"):
                top = sorted(worst[k], reverse=True)[:12]
                top = [t for t in top if t[0] > 1.0]
                if top:
                    print(md_table(["fixture", k, "classic", e, "ratio"], [[f, k, c, a, fmt(q, "{:.3f}")] for q, f, c, a in top]))
                    print()
    if args.sim:
        rows = []
        mism = 0
        for r in ok:
            per = {e: r["engines"][e].get("sim") for e in engines}
            base = per.get("classic")
            for e in engines:
                s = per[e]
                if not s or "rc" in s:
                    rows.append([r["fixture"], e, "FAIL", "", "", ""])
                    continue
                same = "-" if base is None or "rc" in base else ("same" if s["digest"] == base["digest"] else "DIFFERENT")
                mism += same == "DIFFERENT"
                cps = s["cycles"] / (s["sim_ns"] / 1e9) if s["sim_ns"] else 0
                rows.append([r["fixture"], e, f"{s['pass']}/{s['tests']}", s["instructions"], fmt(s.get("static_instr")), fmt(cps), same])
        print(f"## simulation (out_digest vs classic; DIFFERENT={mism})\n")
        print(md_table(["fixture", "engine", "pass", "instructions (perf)", "static instr (objdump)", "cycles/s", "out_digest"], rows[: args.rows]))
        print()
        # per-engine static instruction ratios against classic
        for e in engines:
            if e == "classic":
                continue
            ratios, kworse, kratios = [], [], []
            for r in ok:
                a, c = r["engines"].get(e, {}).get("sim"), r["engines"].get("classic", {}).get("sim")
                if a and c and "rc" not in a and "rc" not in c and a.get("static_instr") and c.get("static_instr"):
                    ratios.append(a["static_instr"] / c["static_instr"])
                    if a.get("kernel_instr") and c.get("kernel_instr"):
                        kq = a["kernel_instr"] / c["kernel_instr"]
                        kratios.append(kq)
                        if kq > 1.0:
                            kworse.append((kq, r["fixture"], c["kernel_instr"], a["kernel_instr"]))
            if kratios:
                # primary sim-speed line: kernel-only static instructions (per-cycle cost); static, not weighted by trip counts
                print(f"static kernel instructions {e}/classic (PRIMARY sim-cost metric; static, not dynamic): geomean {geomean(kratios):.4f} "
                      f"over {len(kratios)} fixtures, worst {max(kratios):.4f}, better {sum(1 for q in kratios if q < 1.0)}, worse {len(kworse)}")
            if ratios:
                print(f"whole-fixture static instructions {e}/classic (includes fixed per-module/runtime code, dilutes regressions; secondary): "
                      f"geomean {geomean(ratios):.4f} over {len(ratios)} fixtures, worst {max(ratios):.4f}\n")
            if kworse:
                print(md_table(["fixture", "classic kernel", e + " kernel", "kernel ratio"], [[f, c, a, fmt(q, "{:.4f}")] for q, f, c, a in sorted(kworse, reverse=True)[:30]]))
                print()

    bad = [r for r in results if r["status"] != "ok"]
    if bad:
        print("## failures\n")
        print(md_table(["fixture", "status", "detail"], [[r["fixture"], r["status"], r["detail"][:120]] for r in bad[:80]]))
        print()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--corpus", default="equiv", help="equiv | sim | lgraph | path:DIR | lhdtrack:DIR (default equiv)")
    ap.add_argument("--engines", default=",".join(ENGINES_ALL), help="comma list of classic,aegraph,aegraph+classic")
    ap.add_argument("--filter", default="", help="regex on the fixture name")
    ap.add_argument("--limit", type=int, default=0, help="first N fixtures")
    ap.add_argument("-j", "--jobs", type=int, default=max(1, (os.cpu_count() or 2) // 4))
    ap.add_argument("--timeout", type=int, default=300, help="seconds per lhd command")
    ap.add_argument("--lec", action="store_true", help="LEC A==B (engine lg vs classic lg) and B==source")
    ap.add_argument("--lec-timeout", type=int, default=60)
    ap.add_argument("--width-agnostic", action="store_true", help="LEC of the aegraph result with compile.bitfuzz.mode=wires vs without it (gate G1)")
    ap.add_argument("--determinism", action="store_true", help="compile aegraph twice, lhd tool diff --structural")
    ap.add_argument("--synth", action="store_true", help="lhd synth QoR with the in-repo test.lib")
    ap.add_argument("--sim", action="store_true", help="lhd sim per engine: out_digest, instructions, cycles/s")
    ap.add_argument("--set", dest="extra_sets", action="append", default=[], metavar="K=V",
                    help="extra --set for the design under test (compile/synth/sim; never lec), e.g. compile.bitfuzz.mode=wires")
    ap.add_argument("--synth-cost", default="synth", help="compile.aegraph.cost of the --synth leg (default synth)")
    ap.add_argument("--sim-cost", default="sim", help="compile.aegraph.cost of the --sim leg (default sim; auto = sim under lhd sim)")
    ap.add_argument("--rows", type=int, default=60, help="cap rows of the synth/sim tables")
    ap.add_argument("--out", default=os.path.join(REPO, "tmp_cprop_engine_sweep"), help="work root")
    ap.add_argument("--json", default="", help="also write the raw results here")
    args = ap.parse_args()

    args.engines = [e for e in args.engines.split(",") if e]
    for e in args.engines:
        if e not in ENGINES_ALL:
            sys.exit(f"unknown engine '{e}' (classic|aegraph|aegraph+classic)")
    if not os.path.exists(LHD):
        sys.exit(f"{LHD} not found: bazel build -c opt //lhd:lhd (or set $LHD)")

    files = corpus_files(args.corpus)
    if args.filter:
        files = [f for f in files if re.search(args.filter, os.path.basename(f))]
    if args.limit:
        files = files[: args.limit]
    os.makedirs(args.out, exist_ok=True)
    print(f"[sweep] {len(files)} fixtures x {len(args.engines)} engines, -j {args.jobs}, lhd={LHD}", file=sys.stderr)

    results = []
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = {pool.submit(one, f, args, args.out): f for f in files}
        for i, fut in enumerate(cf.as_completed(futs), 1):
            try:
                results.append(fut.result())
            except Exception as exc:  # a harness bug must not lose the sweep
                results.append({"fixture": os.path.basename(futs[fut])[:-4], "status": "HARNESS_ERROR", "detail": repr(exc), "engines": {}})
            if i % 25 == 0:
                print(f"[sweep] {i}/{len(files)}", file=sys.stderr)
    results.sort(key=lambda r: r["fixture"])
    if args.json:
        with open(args.json, "w") as f:
            json.dump(results, f, indent=1)
    report([r for r in results], args)
    if not os.listdir(args.out):
        os.rmdir(args.out)


if __name__ == "__main__":
    main()
