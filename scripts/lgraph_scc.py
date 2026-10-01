#!/usr/bin/env python3
"""Report the combinational SCCs of an LGraph.  DIAGNOSTIC ONLY.

Reads what `lhd tool cat --target all --max 0 --diag-fmt jsonl` prints and
computes strongly connected components over it.  It deliberately does NOT live
in a pass: nothing here may change what a pass emits, and the whole point is to
stay runnable on a graph pass.lean refuses.

WHY A SEPARATE TOOL.  pass.lean names exactly ONE back edge and stops
(`reachable_topo_order`, pass_lean.cpp).  That is right for a refusal but not
enough to CLASSIFY the cycle: a back edge is one (reader, driver) pair, while
"genuine RTL loop / packed aggregate self-reference / latch artifact / lowering
defect" is a question about the whole component -- its ops, its widths, which
pins the edges enter, and where the members came from in the RTL.

EDGE CUTTING.  `--cut lean` reproduces pass.lean's rule so an SCC reported here
is one pass.lean would walk into:

    an edge is cut when its DRIVER is a graph input, a constant, or a flop

(pass_lean.cpp:1292; `flop` and `fflop` are what `node_is_flop` covers).  Note
what is NOT cut: a Memory node's read output.  pass.lean puts flops in
`flop_nids` and memories nowhere, so a memory read feeds the combinational walk
like any other cell.  `--cut lean-plus-mem` additionally cuts memory outputs, so
the two runs together answer "does this cycle close only through a memory read?"
without reading any pass code.  `--cut none` cuts no CELL KIND; a graph-IO
endpoint (`$name`) is excluded under every policy because it is not a node, and
is counted separately as `edges_cut_io`.

FAIL-CLOSED.  Every endpoint must resolve.  An endpoint is either a graph IO pin
(`$name`, legitimately cut) or `<debug_name>.<pin>` naming a node this dump also
declared.  Anything else -- a truncated dump, a renamed endpoint format, a
non-JSON line -- is an ERROR and exits non-zero, because the alternative is to
silently drop edges and report SCCS=0 on a graph that has cycles.
"""
import argparse
import json
import sys
from collections import defaultdict

CUTS = ("none", "lean", "lean-plus-mem")

EXIT_OK = 0
EXIT_USAGE = 2
EXIT_UNSOUND = 3  # the input could not be trusted; any SCC count would be a guess


class Unsound(Exception):
    pass


def load(path):
    """-> (nodes, pin_records, raw_edges).  A line that is not JSON is fatal:
    `lhd tool cat` prints a plain-text notice when it truncates at --max, and
    skipping that notice would silently analyse a partial graph."""
    nodes, pin_records, raw_edges = {}, [], []
    with open(path) as fh:
        for lineno, line in enumerate(fh, 1):
            line = line.strip()
            if not line:
                continue
            try:
                r = json.loads(line)
            except json.JSONDecodeError:
                raise Unsound(f"{path}:{lineno} is not JSON -- the dump is truncated or "
                              f"interleaved with plain text: {line[:120]!r}")
            t = r.get("t")
            if t == "node":
                nodes[r["nid"]] = {"nid": r["nid"], "kind": r["kind"], "name": r.get("name"),
                                   "src": r.get("src")}
            elif t == "pin":
                pin_records.append(r)
            elif t == "edge":
                raw_edges.append(r)
            else:
                raise Unsound(f"{path}:{lineno} has unknown record type {t!r}")
    if not nodes:
        raise Unsound(f"{path} declares no nodes")
    return nodes, pin_records, raw_edges


def endpoint_index(nodes):
    """`lhd tool` prints an endpoint as `<debug_name>.<pin>`; debug_name is
    `kind_nid`, plus `:name` when the node carries one.  A node NAME may itself
    contain dots (yosys hierarchical wires do: `u_inst_data.rf_q`), so the split
    must be by longest known prefix, never by rsplit."""
    idx = {}
    for n in nodes.values():
        dn = f"{n['kind']}_{n['nid']}"
        if n["name"]:
            dn += ":" + n["name"]
        idx[dn] = n["nid"]
    return idx


def resolve(ep, idx):
    """-> ('io', name) | ('node', nid, pin) | ('bad', ep).

    'bad' is NEVER silently treated as a cut edge; the caller fails the run.
    That distinction is the whole point: an unrecognised endpoint and a graph-IO
    endpoint both have "no node", but only one of them is expected."""
    if ep.startswith("$"):
        return ("io", ep)
    cut = len(ep)
    while True:
        cut = ep.rfind(".", 0, cut)
        if cut < 0:
            return ("bad", ep)
        head = ep[:cut]
        if head in idx:
            return ("node", idx[head], ep[cut + 1:])


def tarjan(adj, verts):
    """Iterative Tarjan; recursion would overflow on a 19k-node cone."""
    index, low, on, stack, out = {}, {}, set(), [], []
    counter = [0]
    for root in verts:
        if root in index:
            continue
        work = [(root, iter(adj.get(root, ())))]
        index[root] = low[root] = counter[0]
        counter[0] += 1
        stack.append(root)
        on.add(root)
        while work:
            v, it = work[-1]
            advanced = False
            for w in it:
                if w not in index:
                    index[w] = low[w] = counter[0]
                    counter[0] += 1
                    stack.append(w)
                    on.add(w)
                    work.append((w, iter(adj.get(w, ()))))
                    advanced = True
                    break
                if w in on:
                    low[v] = min(low[v], index[w])
            if advanced:
                continue
            work.pop()
            if work:
                low[work[-1][0]] = min(low[work[-1][0]], low[v])
            if low[v] == index[v]:
                comp = []
                while True:
                    w = stack.pop()
                    on.discard(w)
                    comp.append(w)
                    if w == v:
                        break
                out.append(comp)
    return out


def analyse(path, cut):
    nodes, pin_records, raw_edges = load(path)
    idx = endpoint_index(nodes)

    # Driver-pin widths come from the EDGE records, which carry the width of the
    # exact driver pin.  The `pin` records cannot be used for this: they print
    # `name` but not `port_id`, so a multi-output node (Memory, Sub, IO) collapses
    # all its unnamed driver pins onto one key.  Where a pin record IS
    # unambiguous it is cross-checked below.
    driver_pins = defaultdict(dict)   # nid -> {pin label: bits}

    unresolved = []
    io_cut = 0
    kind_cut = 0
    edges = []
    adj = defaultdict(list)

    cut_kinds = set()
    if cut in ("lean", "lean-plus-mem"):
        cut_kinds |= {"flop", "fflop", "const"}
    if cut == "lean-plus-mem":
        cut_kinds.add("memory")

    for e in raw_edges:
        d = resolve(e["from"], idx)
        s = resolve(e["to"], idx)
        for r, which in ((d, "from"), (s, "to")):
            if r[0] == "bad":
                unresolved.append((which, r[1]))
        if d[0] == "bad" or s[0] == "bad":
            continue
        if d[0] == "node":
            driver_pins[d[1]][d[2]] = e.get("bits")
        if d[0] == "io" or s[0] == "io":
            io_cut += 1
            continue
        if nodes[d[1]]["kind"] in cut_kinds:
            kind_cut += 1
            continue
        rec = {"d": d[1], "dp": d[2], "s": s[1], "sp": s[2], "bits": e.get("bits")}
        edges.append(rec)
        adj[s[1]].append(d[1])     # sink DEPENDS ON driver: the direction pass.lean walks

    if unresolved:
        raise Unsound(
            f"{len(unresolved)} edge endpoint(s) in {path} name no declared node and are "
            f"not graph IO (`$name`).  Examples: "
            + "; ".join(f"{w}={ep!r}" for w, ep in unresolved[:5])
            + ".  Refusing to report an SCC count computed with those edges dropped.")

    # Cross-check: where a pin record's (nid, name) matches an edge-derived label,
    # the widths must agree.  A disagreement means the two views of the dump have
    # drifted and neither can be trusted.
    for r in pin_records:
        nm = r.get("name")
        if nm is None:
            continue
        got = driver_pins.get(r["nid"], {}).get(nm, "absent")
        if got != "absent" and got != r.get("bits"):
            raise Unsound(f"pin record {r['nid']}.{nm} says {r.get('bits')}b but its edges "
                          f"say {got}b")

    comps = [c for c in tarjan(adj, list(nodes)) if len(c) > 1]
    comps += [[n] for n in sorted({r["d"] for r in edges if r["d"] == r["s"]})]
    comps.sort(key=len, reverse=True)
    return {"nodes": nodes, "driver_pins": driver_pins, "edges": edges,
            "raw_edges": raw_edges, "io_cut": io_cut, "kind_cut": kind_cut, "comps": comps}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("jsonl", help="output of `lhd tool cat --target all --max 0 --diag-fmt jsonl`")
    ap.add_argument("--cut", default="lean", choices=CUTS)
    ap.add_argument("--stage", default="", help="label for the report header")
    ap.add_argument("--max-report", type=int, default=20, help="SCCs to detail (0 = all)")
    ap.add_argument("--max-members", type=int, default=60,
                    help="members / internal edges to detail per SCC (0 = all)")
    ap.add_argument("--require-member", action="append", default=[],
                    help="fail unless this nid is inside some reported SCC (repeatable)")
    args = ap.parse_args()

    try:
        a = analyse(args.jsonl, args.cut)
    except Unsound as e:
        print(f"LGRAPH-SCC-UNSOUND stage={args.stage or '?'} cut={args.cut}: {e}")
        return EXIT_UNSOUND
    except OSError as e:
        print(f"LGRAPH-SCC-UNSOUND stage={args.stage or '?'} cut={args.cut}: {e}")
        return EXIT_USAGE

    nodes, comps, edges = a["nodes"], a["comps"], a["edges"]
    print(f"LGRAPH-SCC stage={args.stage or '?'} cut={args.cut} file={args.jsonl}")
    print(f"  nodes={len(nodes)} edges_total={len(a['raw_edges'])} edges_kept={len(edges)} "
          f"edges_cut_io={a['io_cut']} edges_cut_kind={a['kind_cut']}")
    print(f"  SCCS={len(comps)} members_in_sccs={sum(len(c) for c in comps)}")

    member_of = {}
    for i, c in enumerate(comps):
        for n in c:
            member_of[n] = i
    internal = defaultdict(list)
    for r in edges:
        if member_of.get(r["d"]) is not None and member_of.get(r["d"]) == member_of.get(r["s"]):
            internal[member_of[r["d"]]].append(r)

    missing = [m for m in args.require_member if int(m) not in member_of]
    if not comps:
        print("  (no combinational SCC under this cut)")

    show = comps if args.max_report == 0 else comps[:args.max_report]
    for i, c in enumerate(show):
        print(f"\n--- SCC #{i} size={len(c)} internal_edges={len(internal[i])} ---")
        kinds = defaultdict(int)
        for n in c:
            kinds[nodes[n]["kind"]] += 1
        print("  ops: " + ", ".join(f"{k}={v}" for k, v in sorted(kinds.items())))
        srcs = sorted({nodes[n]["src"] for n in c if nodes[n]["src"]})
        print(f"  rtl sources ({len(srcs)}): " + (", ".join(srcs[:8]) if srcs else "none recorded"))
        mem = sorted(c)
        shown = mem if not args.max_members else mem[:args.max_members]
        print("  members (nid kind name src | driver pins):")
        for n in shown:
            nd = nodes[n]
            dp = a["driver_pins"].get(n, {})
            pins = ", ".join(f"{k}={v}b" for k, v in sorted(dp.items())) or "no out-edge in this dump"
            print(f"    n_{n:<8} {nd['kind']:<9} {nd['name'] or '-'}  {nd['src'] or '-'}  | {pins}")
        if args.max_members and len(mem) > args.max_members:
            print(f"  ... and {len(mem) - args.max_members} more members")
        ie = internal[i]
        lim = ie if not args.max_members else ie[:args.max_members]
        print("  internal edges (driver.pin -> sink.pin  width):")
        for r in lim:
            print(f"    {nodes[r['d']]['kind']}_{r['d']}.{r['dp']} -> "
                  f"{nodes[r['s']]['kind']}_{r['s']}.{r['sp']}  {r['bits']}b")
        if args.max_members and len(ie) > args.max_members:
            print(f"  ... and {len(ie) - args.max_members} more internal edges")
    if args.max_report and len(comps) > args.max_report:
        print(f"\n... and {len(comps) - args.max_report} more SCCs")

    if missing:
        print("\nFAIL: --require-member node(s) not in any SCC: " + ", ".join(missing))
        return EXIT_UNSOUND
    if args.require_member:
        print("\nok: every --require-member node is inside a reported SCC")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
