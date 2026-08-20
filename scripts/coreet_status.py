#!/usr/bin/env python3
"""Classify every CORE-ET minion module from the artifacts the sweep left behind.

Reads generated/core-et/<module>/logs/* and emits a work-list TSV plus
COREET_STATUS.md.  Classification comes from the RUN, never from the source:
CVA6_COVERAGE_PLAN.md records a static regex mis-classifying `lzc` and
propagating a false "memory-blocked" to `alu`/`pmp`, both already proven.
"""
import json, os, re, sys, glob, collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "generated", "core-et")


def jload(p):
    try:
        with open(p) as f:
            return json.load(f)
    except Exception:
        return None


def err_of(p):
    d = jload(p)
    return "" if not d else (d.get("error") or {}).get("message", "")


def classify(m):
    """-> (stage, verdict, reason_class, detail)"""
    L = os.path.join(GEN, m, "logs")
    gates = os.path.join(L, "static_gates.log")

    # --- compile
    cerr = err_of(os.path.join(L, "lhd_compile_result.json"))
    if cerr or not os.path.isdir(os.path.join(GEN, m, "lgdb_raw")):
        # The slang diagnostic surfaces on the driver's own stdout
        # (logs/lhd_compile.log), NOT in the per-step yosys log -- read both, the
        # driver log first.  (Reading only the step log reported every one of
        # these as a bare "yosys failed".)
        cand = [os.path.join(GEN, m, "logs", "lhd_compile.log")] + sorted(
            glob.glob(os.path.join(GEN, m, "lhd_work", "logs", "*yosys*log")))
        detail = ""
        for y in cand:
            if not os.path.exists(y):
                continue
            with open(y, errors="replace") as f:
                txt = f.read()
            mm = re.search(r"error: (.+?)(?:\\n|$)", txt, re.M)
            if mm:
                detail = mm.group(1).strip()[:150]
                break
        if not detail:
            detail = cerr[:150]
        if "used before its declaration" in detail:
            return "compile", "BLOCKED", "rtl-use-before-decl", detail
        if "DPI" in detail:
            return "compile", "BLOCKED", "rtl-dpi-sim-only", detail
        return "compile", "BLOCKED", "frontend", detail or "yosys failed"

    # --- single_edge
    selog = os.path.join(L, "single_edge.log")
    se_msg = ""
    if os.path.exists(selog):
        with open(selog, errors="replace") as f:
            msgs = re.findall(r'"message":"([^"]*)"', f.read())
        se_msg = msgs[-1] if msgs else ""
    if "refused" in se_msg:
        cls = "memory" if "memory `" in se_msg else (
            "multi-clock" if "clock nets" in se_msg else "single-edge")
        return "single_edge", "BLOCKED", cls, se_msg[:150]

    # --- lean emit
    lerr = err_of(os.path.join(L, "lhd_lean_result.json"))
    if lerr:
        cls = "memory" if ("Memory" in lerr or "memory" in lerr) else "lean-emit"
        return "lean-emit", "BLOCKED", cls, lerr[:150]

    # --- static gates
    if not os.path.exists(gates):
        return "lean-emit", "BLOCKED", "no-gates", "static gates never ran"
    g = open(gates, errors="replace").read()
    if "gate_status=0" not in g:
        unh = re.findall(r"^\s+(Op_\w+)\s+arity\s+(\d+)\s+(\d+) nodes\s+\[unhandled\]", g, re.M)
        if unh:
            return ("static-gates", "NEEDS-BRIDGE", "op-bridge",
                    ", ".join(f"{o} arity {a} ({n} nodes)" for o, a, n in unh))
        if "NO certificate nodes found" in g:
            return "static-gates", "DEGENERATE", "no-cert-nodes", \
                   "module has no computed nodes (constant/pass-through only)"
        return "static-gates", "BLOCKED", "static-gate", "gate_status=1"
    return "static-gates", "READY", "", ""


def metrics(m):
    g = os.path.join(GEN, m, "logs", "static_gates.log")
    n = f = w = ""
    if os.path.exists(g):
        t = open(g, errors="replace").read()
        for pat, key in ((r"cert nodes\s+:\s+(\d+)", "n"),
                         (r"state fields\s+:\s+(\d+)", "f"),
                         (r"node output widths : max=(\d+)", "w")):
            mm = re.search(pat, t)
            if mm:
                locals()  # noqa
                if key == "n": n = mm.group(1)
                elif key == "f": f = mm.group(1)
                else: w = mm.group(1)
    p = 1
    sl = os.path.join(GEN, m, "logs", "single_edge.log")
    if os.path.exists(sl):
        mm = re.findall(r"P=(\d+) slots", open(sl, errors="replace").read())
        if mm: p = int(mm[-1])
    return n, f, w, p



REASON_TEXT = {
    "rtl-use-before-decl":
        "CORE-ET RTL uses an identifier before declaring it. The LRM forbids it, "
        "Verilator tolerates it, slang rejects it. Worked around by a derived copy "
        "(`scripts/coreet_patch_srcs.py`); the real fix belongs in core-et.",
    "rtl-dpi-sim-only":
        "Simulation-only DPI import; excluded from the real filelist by design.",
    "memory":
        "The design contains a memory node. `emit_fast_bridge` is hard-gated on "
        "`memory_nodes.empty()` and the memory certificate is still a counts-only "
        "stub (Phase 3/4 of CVA6_COVERAGE_PLAN.md). OUT OF SCOPE for this effort.",
    "multi-clock":
        "`pass.single_edge` refuses: two clock nets with no known integer ratio, so "
        "it cannot put both domains on one time base. Fails closed by design.",
    "op-bridge":
        "Reaches an operator the step-5 dispatch does not handle yet. Needs one "
        "`OpBridge.lean` lemma plus a dispatch arm.",
    "no-cert-nodes":
        "Degenerate: the module computes nothing (outputs are constants or "
        "pass-throughs), so there are no certificate nodes to bridge.",
    "frontend":
        "Front-end elaboration failed for another reason; see the module's "
        "`logs/lhd_compile.log`.",
}


def write_md(rows):
    """Emit COREET_STATUS.md: the per-module record, blockers enumerated."""
    out = os.path.join(GEN, "COREET_STATUS.md")
    ready = [r for r in rows if r[2] == "READY"]
    ready.sort(key=lambda r: int(r[4]) if r[4] else 0)
    others = collections.defaultdict(list)
    for r in rows:
        if r[2] != "READY":
            others[r[3] or r[2]].append(r)

    L = []
    L.append("# CORE-ET minion — fast model vs graph certificate\n")
    L.append("Generated by `scripts/coreet_status.py` from the artifacts under "
             "`generated/core-et/`. Every row comes from **running** the pipeline "
             "(compile -> `pass single_edge` -> `pass.lean` emit -> static gates), "
             "never from pattern-matching the source: `CVA6_COVERAGE_PLAN.md` "
             "records a static regex mis-classifying `lzc` as memory-blocked and "
             "propagating that to `alu`/`pmp`, both already proven.\n")
    L.append(f"**{len(rows)} minion modules** — "
             + ", ".join(f"{v} {k}" for k, v in
                         collections.Counter(r[2] for r in rows).most_common()) + "\n")

    L.append("## Scope\n")
    L.append("Prove the memory-free set; enumerate the rest. Building the memory "
             "certificate is explicitly out of scope, so every module whose LGraph "
             "holds a memory node is listed below as deferred rather than left "
             "silent.\n")

    L.append(f"## READY — {len(ready)} modules, gates green\n")
    L.append("`_comb`/`_next`/`_step` bridge emitted, `op_census.py` and "
             "`const_parity.py` PASS, 0 sorries. Smallest first: node count is the "
             "queue order, though it does **not** predict wall time "
             "(`cva6_tlb_gate` at 2,061 nodes took 5.2 h because one serial "
             "declaration dominated).\n")
    L.append("| module | nodes | flops | max width | slots (P) | theorems |")
    L.append("|---|---|---|---|---|---|")
    for m, st, v, cl, n, f, w, p, d in ready:
        th = "comb/next/step" if (f and f != "0") else "comb"
        L.append(f"| `{m}` | {n or '-'} | {f or '-'} | {w or '-'} | {p} | {th} |")
    L.append("")

    order = ["memory", "rtl-use-before-decl", "op-bridge", "multi-clock",
             "no-cert-nodes", "rtl-dpi-sim-only", "frontend"]
    L.append("## Not ready — every module, with its blocker\n")
    for key in order + [k for k in others if k not in order]:
        rs = others.get(key)
        if not rs:
            continue
        L.append(f"### {key} — {len(rs)} modules\n")
        L.append(REASON_TEXT.get(key, "") + "\n")
        L.append("| module | stage | detail |")
        L.append("|---|---|---|")
        for m, st, v, cl, n, f, w, p, d in sorted(rs):
            L.append(f"| `{m}` | {st} | {d[:110].replace('|', '\\|')} |")
        L.append("")

    with open(out, "w") as fh:
        fh.write("\n".join(L) + "\n")
    print(f"report -> {out}")


STAGE_LOG = {
    "compile":      "logs/lhd_compile.log",
    "single_edge":  "logs/single_edge.log",
    "lean-emit":    "logs/lhd_lean_result.json",
    "static-gates": "logs/static_gates.log",
}


def write_failure_log(rows):
    """Every module that did NOT reach READY, one block each, with the verbatim
    diagnostic and the path to the full log.  Kept as its own file so a failure is
    never something you have to go digging for."""
    out = os.path.join(GEN, "coreet_failures.log")
    bad = [r for r in rows if r[2] != "READY"]
    groups = collections.defaultdict(list)
    for r in bad:
        groups[r[3] or r[2]].append(r)

    order = ["memory", "multi-clock", "op-bridge", "frontend", "rtl-use-before-decl",
             "rtl-dpi-sim-only", "no-cert-nodes", "lean-emit", "no-gates"]

    L = []
    L.append("CORE-ET minion -- modules that did not reach READY")
    L.append("=" * 70)
    L.append("")
    L.append(f"{len(bad)} of {len(rows)} modules. Generated by scripts/coreet_status.py.")
    L.append("Every entry is the result of RUNNING the pipeline, not of reading source.")
    L.append("Artifacts for module M live under generated/core-et/M/.")
    L.append("")
    L.append("SUMMARY")
    L.append("-" * 70)
    for k in order + [k for k in groups if k not in order]:
        if k in groups:
            L.append(f"  {len(groups[k]):4d}  {k}")
    L.append("")

    for k in order + [k for k in groups if k not in order]:
        rs = groups.get(k)
        if not rs:
            continue
        L.append("")
        L.append("=" * 70)
        L.append(f"{k.upper()}  ({len(rs)} modules)")
        L.append("=" * 70)
        why = REASON_TEXT.get(k, "")
        if why:
            for line in _wrap(why, 68):
                L.append("  " + line)
        L.append("")
        for m, st, v, cl, n, f, w, p, d in sorted(rs):
            L.append(f"--- {m}")
            L.append(f"    stage   : {st}")
            if n:
                L.append(f"    metrics : nodes={n} flops={f or '-'} max_w={w or '-'} slots={p}")
            for line in _wrap(d or "(no diagnostic captured)", 66):
                L.append(f"    {line}")
            lg = STAGE_LOG.get(st)
            if lg:
                L.append(f"    log     : generated/core-et/{m}/{lg}")
            L.append("")

    with open(out, "w") as fh:
        fh.write("\n".join(L) + "\n")
    print(f"failures -> {out}  ({len(bad)} modules)")


def _wrap(text, width):
    words, line, res = text.split(), "", []
    for wd in words:
        if len(line) + len(wd) + 1 > width:
            res.append(line)
            line = wd
        else:
            line = (line + " " + wd).strip()
    if line:
        res.append(line)
    return res or [""]

def main(list_file):
    mods = [l.strip() for l in open(list_file) if l.strip() and not l.startswith("#")]
    rows = []
    for m in mods:
        if not os.path.isdir(os.path.join(GEN, m)):
            rows.append((m, "-", "NOT-RUN", "", "", "", "", 1, ""))
            continue
        stage, verdict, cls, detail = classify(m)
        n, f, w, p = metrics(m)
        rows.append((m, stage, verdict, cls, n, f, w, p, detail))

    tsv = os.path.join(GEN, "coreet_worklist.tsv")
    with open(tsv, "w") as fh:
        fh.write("module\tstage\tverdict\treason\tnodes\tflops\tmax_w\tslots\tdetail\n")
        for r in rows:
            fh.write("\t".join(str(x).replace("\t", " ") for x in r) + "\n")

    write_md(rows)
    write_failure_log(rows)

    by_v = collections.Counter(r[2] for r in rows)
    by_r = collections.Counter(r[3] for r in rows if r[2] not in ("READY",))
    print(f"{len(rows)} modules -> {tsv}")
    for k, v in by_v.most_common():
        print(f"  {v:4d}  {k}")
    print("  reasons:")
    for k, v in by_r.most_common():
        if k:
            print(f"    {v:4d}  {k}")
    return rows


if __name__ == "__main__":
    main(sys.argv[1])
