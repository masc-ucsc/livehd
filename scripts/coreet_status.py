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
