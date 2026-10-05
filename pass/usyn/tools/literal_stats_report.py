#!/usr/bin/env python3
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Render pass.usyn.literal_stats JSON lines into an HTML report.

usage: literal_stats_report.py OUT.html [--notes=NOTES.html] CORE=stats.jsonl ...

Each stats.jsonl holds one record per synthesized region (literal_stats.hpp).
Optional tmap records (kind "tmap", added by the (b) experiment) are rendered
when present.
"""
import html
import json
import sys

# The latch scenarios (reg_lat, stat_lat) stay in the JSON but are not rendered:
# latches here are clock gating / native boundaries (see the report notes).
SCEN = [
    ("reg", "flop Q + ports"),
    ("stat", "+ static logic over registered signals"),
]
CLASSES = ["flop", "port", "latch_low", "latch_other", "other", "stat", "stat_lat"]


def load(path):
    regions, tmaps = [], []
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        d = json.loads(line)
        (tmaps if d.get("kind") == "tmap" else regions).append(d)
    return regions, tmaps


def pct(a, b):
    return "&ndash;" if not b else f"{100.0 * (a - b) / b:+.1f}%"


def main():
    out = sys.argv[1]
    cores = []
    notes = ""
    for arg in sys.argv[2:]:
        if arg.startswith("--notes="):
            notes = open(arg.split("=", 1)[1]).read()
            continue
        name, path = arg.split("=", 1)
        regions, tmaps = load(path)
        cores.append((name, regions, tmaps))

    h = []
    w = h.append
    w("<!doctype html><html><head><meta charset='utf-8'><title>USYN literal-network experiment</title><style>")
    w("body{font-family:system-ui,sans-serif;max-width:1200px;margin:2em auto;line-height:1.45;color:#222}")
    w("table{border-collapse:collapse;margin:1em 0;font-size:13px}th,td{border:1px solid #ccc;padding:3px 7px;text-align:right}")
    w("th{background:#f2f2f2}td.l,th.l{text-align:left}.good{color:#0a6b2d;font-weight:600}.muted{color:#777}")
    w("code{background:#f4f4f4;padding:0 3px}</style></head><body>")
    w("<h1>USYN literal-network experiment (statistics only)</h1>")
    w("<p>Question: if a small mux network (the <em>literal network</em>), settled in the first half of the cycle by early "
      "control signals, chose the input literals of each selected DOMINO gate, how much simpler would the gate be? "
      "For every selected cell with at most 8 inputs and each depth n = 1..3, the cell function is Shannon-cofactored over n "
      "eligible controls (all subsets tried). <b>Lower bound</b>: the largest re-synthesized cofactor gate (any shared gate must "
      "implement every cofactor). <b>Template</b>: one cofactor's own gate that realizes every cofactor by substituting its inputs "
      "with remaining inputs, complements or constants (exact bounded search, every hit re-verified over all input "
      "assignments). The literal network is reported separately as 2:1 mux count; it is not charged to the DOMINO gate, "
      "because it settles in the otherwise idle first half. Selection and netlists are unchanged.</p>")
    w("<p>Setup: lhdsuite Pyrope sources, <code>lhd synth --set synth.mapper=usyn</code>, asap7 at 1000&nbsp;ps, "
      "<code>pass.satopt=false</code>, <code>pass.usyn.tmap_trials=1</code>, "
      "<code>pass.usyn.literal_stats=FILE</code>. DOMINO cost is usyn's proxy: formula transistors + 5 per Domino / 8 per "
      "DominoLatch.</p>")
    w("<h2>Control eligibility scenarios</h2><table><tr><th class=l>scenario</th><th class=l>eligible controls</th></tr>")
    for k, d in SCEN:
        w(f"<tr><td class=l><code>{k}</code></td><td class=l>{html.escape(d)}</td></tr>")
    w("</table>")

    # Overview.
    w("<h2>Overview</h2><table><tr><th class=l>core</th><th>regions</th><th>cells</th><th>analyzed (&le;8 in)</th>"
      "<th>&gt;8 inputs</th><th>verify failures</th>" + "".join(f"<th>in:{c}</th>" for c in CLASSES) + "</tr>")
    for name, regions, _ in cores:
        tot = {k: 0 for k in ("cells", "analyzed", "wide", "verify_failures")}
        cen = {c: 0 for c in CLASSES}
        for r in regions:
            for k in tot:
                tot[k] += r.get(k, 0)
            for c in CLASSES:
                cen[c] += r.get("inputs", {}).get(c, 0)
        w(f"<tr><td class=l>{name}</td><td>{len(regions)}</td><td>{tot['cells']}</td><td>{tot['analyzed']}</td>"
          f"<td>{tot['wide']}</td><td>{tot['verify_failures']}</td>" + "".join(f"<td>{cen[c]}</td>" for c in CLASSES) + "</tr>")
    w("</table>")
    if notes:
        w(notes)
    w("<p class=muted>in:* counts native (non-producer) cell inputs by class; <code>stat</code> = interior signal whose "
      "whole support is registered.</p>")

    # Per scenario: one table, rows = core x depth.
    for key, desc in SCEN:
        w(f"<h2>Scenario <code>{key}</code> &mdash; {html.escape(desc)}</h2>")
        w("<table><tr><th class=l>core</th><th>n</th><th>cells w/ &ge;n controls</th><th>template found</th>"
          "<th>DOMINO cost base</th><th>template</th><th>&Delta;</th><th>lower bound</th><th>&Delta; lb</th>"
          "<th>stack base</th><th>template</th><th>&Delta;</th><th>cells w/ shorter stack</th>"
          "<th>mux2</th><th>mux2 / found cell</th><th>&Delta; of all DOMINO cost</th></tr>")
        for name, regions, _ in cores:
            design_base = sum(r.get("base_cost", 0) for r in regions)
            for n in (1, 2, 3):
                agg = {}
                for r in regions:
                    for row in r["scenarios"][key]:
                        if row["depth"] == n:
                            for k, v in row.items():
                                agg[k] = agg.get(k, 0) + v
                if not agg or not agg.get("cells"):
                    w(f"<tr><td class=l>{name}</td><td>{n}</td><td>0</td>" + "<td class=muted>&ndash;</td>" * 13 + "</tr>")
                    continue
                cls = lambda a, b: " class=good" if b and (a - b) / b <= -0.10 else ""
                w(f"<tr><td class=l>{name}</td><td>{n}</td><td>{agg['cells']}</td>"
                  f"<td>{agg['tmpl_found']} ({100.0 * agg['tmpl_found'] / agg['cells']:.0f}%)</td>"
                  f"<td>{agg['base']}</td><td>{agg['tmpl']}</td><td{cls(agg['tmpl'], agg['base'])}>{pct(agg['tmpl'], agg['base'])}</td>"
                  f"<td>{agg['lb']}</td><td>{pct(agg['lb'], agg['base'])}</td>"
                  f"<td>{agg['base_stack']}</td><td>{agg['tmpl_stack']}</td>"
                  f"<td{cls(agg['tmpl_stack'], agg['base_stack'])}>{pct(agg['tmpl_stack'], agg['base_stack'])}</td>"
                  f"<td>{agg['stack_gain_cells']}</td><td>{agg['muxes']}</td>"
                  f"<td>{agg['muxes'] / max(agg['tmpl_found'], 1):.1f}</td>"
                  f"<td>{pct(design_base - agg['base'] + agg['tmpl'], design_base)}</td></tr>")
        w("</table>")

    if any(t for _, _, t in cores):
        w("<h2>(b) Technology-mapped cones (asap7, 1000 ps)</h2>")
        w("<p>Per region, the analyzed cells were emitted twice as standalone combinational networks -- as selected today, and "
          "as literal network + template -- and mapped with the same tmap. Cone-local: shared logic and the rest of the "
          "region are excluded.</p>")
        w("<table><tr><th class=l>core</th><th class=l>scenario</th><th>n</th><th>cones</th>"
          "<th>cells as CMOS: area</th><th>delay ps</th>"
          "<th>literal network alone: area</th><th>gates</th><th>delay ps</th>"
          "<th>network + template as CMOS: area</th><th>delay ps</th></tr>")
        for name, _, tmaps in cores:
            agg = {}
            for t in tmaps:
                k = (t["scenario"], t["depth"])
                a = agg.setdefault(k, {"cones": 0, "area0": 0.0, "area1": 0.0, "gates0": 0, "gates1": 0, "delay0": 0.0, "delay1": 0.0,
                                       "area2": 0.0, "gates2": 0, "delay2": 0.0})
                a["area2"] += t.get("network_area", 0.0)
                a["gates2"] += t.get("network_gates", 0)
                a["delay2"] = max(a["delay2"], t.get("network_delay", 0.0))
                a["cones"] += t["cones"]
                a["area0"] += t["base_area"]
                a["area1"] += t["variant_area"]
                a["gates0"] += t["base_gates"]
                a["gates1"] += t["variant_gates"]
                a["delay0"] = max(a["delay0"], t["base_delay"])
                a["delay1"] = max(a["delay1"], t["variant_delay"])
            for (sc, n), a in sorted(agg.items()):
                w(f"<tr><td class=l>{name}</td><td class=l>{sc}</td><td>{n}</td><td>{a['cones']}</td>"
                  f"<td>{a['area0']:.1f}</td><td>{a['delay0']:.0f}</td>"
                  f"<td>{a['area2']:.1f}</td><td>{a['gates2']}</td><td>{a['delay2']:.0f}</td>"
                  f"<td>{a['area1']:.1f}</td><td>{a['delay1']:.0f}</td></tr>")
        w("</table>")
    w("</body></html>")
    open(out, "w").write("\n".join(h))


if __name__ == "__main__":
    main()
