#!/usr/bin/env python3
"""gen_report.py <results.tsv> <out.html> — render the soomrv V->Pyrope sweep.

TSV columns (translate.sh):
  top  category  kind  GATE  RSLANG  LGSLANG  LGYS  PRPGEN  LECSL  LECYS  ABC  msg
"""
import sys, html, collections

COLS = ["top","category","kind","GATE","RSLANG","LGSLANG","LGYS","PRPGEN","LECSL","LECYS","ABC","msg"]
rows = []
for line in open(sys.argv[1]):
    line = line.rstrip("\n")
    if not line:
        continue
    f = line.split("\t")
    f += [""] * (len(COLS) - len(f))
    rows.append(dict(zip(COLS, f)))

# The 10 tracked failure-point categories (counters).  Most are PRIMARY (the
# module's bucket); the yosys-slang lg/lec and abc ones are CROSS-CHECK counters
# (a module can be PASS overall yet still fail a cross-check), counted independently.
def cnt_primary(cat):
    return sum(1 for r in rows if r["category"].split(":")[0] == cat)

prim = collections.Counter(r["category"].split(":")[0] for r in rows)
n = len(rows)
npass = prim.get("PASS", 0)

# PRIMARY partition (each module in exactly one bucket = its first failing stage)
PRIMARY_ORDER = ["yosys+slang fails","--reader slang fails","lg gen from slang fails",
                 "prp gen fails","lec prp vs from slang fails","lec inconclusive"]
primary = {k: cnt_primary(k) for k in PRIMARY_ORDER}
primary["other"] = sum(v for k,v in prim.items() if k.startswith("other"))
# CROSS-CHECK counters (independent signals; a PASS module may still trip one)
cross = {
  "yosys+slang gate FAIL (single-unit, design-level)": sum(1 for r in rows if r["GATE"]=="FAIL"),
  "lg gen from yosys-slang FAIL":   sum(1 for r in rows if r["LGYS"]=="FAIL"),
  "lec prp vs yosys-slang REFUTED": sum(1 for r in rows if r["LECYS"]=="REFUTED"),
  "abc gen FAIL":                   sum(1 for r in rows if r["ABC"]=="FAIL"),
}

def cell(v):
    color = {"PASS":"#1a7f37","PROVEN":"#1a7f37","FAIL":"#cf222e","REFUTED":"#cf222e",
             "TIMEOUT":"#9a6700","UNKNOWN":"#9a6700","NA":"#8c959f","-":"#8c959f"}.get(v,"#24292f")
    return f'<td style="color:{color};font-weight:600">{html.escape(v)}</td>'

def catclass(cat):
    c = cat.split(":")[0]
    if c=="PASS": return "#1a7f37"
    if c=="lec inconclusive": return "#9a6700"
    return "#cf222e"

out = []
out.append("<!doctype html><meta charset=utf-8><title>soomrv V&rarr;Pyrope sweep</title>")
out.append("""<style>
body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;margin:24px;color:#24292f}
h1{font-size:20px} h2{font-size:16px;margin-top:28px;border-bottom:1px solid #d0d7de;padding-bottom:4px}
table{border-collapse:collapse;font-size:13px;margin:8px 0}
th,td{border:1px solid #d0d7de;padding:3px 8px;text-align:left}
th{background:#f6f8fa} .num{text-align:right;font-variant-numeric:tabular-nums}
.bar{height:14px;background:#1a7f37;display:inline-block;border-radius:2px}
code{background:#f6f8fa;padding:1px 4px;border-radius:3px;font-size:12px}
.msg{font-size:11px;color:#57606a;max-width:520px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
</style>""")
out.append(f"<h1>soomrv Verilog &rarr; Pyrope translation sweep</h1>")
out.append(f"<p><b>{npass} / {n} PASS</b> (slang round-trip LEC-verified). "
           f"Pipeline: <code>--reader slang</code> &rarr; .prp &rarr; lg; LEC (lgyosys) prp-lg vs slang-lg "
           f"and vs yosys-slang-lg; abc map (only if slang-LEC PROVEN). "
           f"yosys+slang gate runs on a <code>!&amp;</code>&rarr;<code>~&amp;</code>-normalized tree.</p>")

# ── PRIMARY partition counters (each module in exactly one) ──
out.append("<h2>Primary classification (each module in exactly one bucket)</h2>")
out.append("<table><tr><th>category</th><th class=num>count</th><th>bar</th></tr>")
out.append(f'<tr><td style="color:#1a7f37;font-weight:700">PASS</td><td class=num>{npass}</td>'
           f'<td><span class=bar style="width:{npass*6}px"></span></td></tr>')
for k in PRIMARY_ORDER + ["other"]:
    v = primary[k]
    out.append(f'<tr><td>{html.escape(k)}</td><td class=num>{v}</td>'
               f'<td><span class=bar style="width:{v*6}px;background:#cf222e"></span></td></tr>')
out.append(f"<tr><th>total</th><th class=num>{npass+sum(primary.values())}</th><th></th></tr></table>")
out.append("<p style='font-size:12px;color:#57606a'>'yosys+slang fails' = the module's OWN file is "
           "one yosys-slang cannot parse (genuinely hard, lower priority); '--reader slang fails' = the "
           "module IS yosys-slang-parseable, so the LiveHD slang-reader gap is the actionable blocker.</p>")
# ── CROSS-CHECK counters (independent signals) ──
out.append("<h2>Cross-check signals (independent &mdash; a PASS module may still trip one)</h2>")
out.append("<table><tr><th>signal</th><th class=num>count</th></tr>")
for k,v in cross.items():
    out.append(f'<tr><td>{html.escape(k)}</td><td class=num>{v}</td></tr>')
out.append("</table>")

# ── dominant blocking issues (by normalized error message) — the prioritization view ──
import re
def norm_msg(m):
    m = re.sub(r"'[^']*'", "'X'", m)
    m = re.sub(r'"[^"]*"', '"X"', m)
    m = re.sub(r'\b\d+\b', 'N', m)
    return m[:110] if m else "(no message)"
groups = collections.defaultdict(list)
for r in rows:
    if r["category"] == "PASS":
        continue
    groups[(r["category"].split(":")[0], norm_msg(r["msg"]))].append(r["top"])
out.append("<h2>Dominant blocking issues (group by category + root-cause message)</h2>")
out.append("<table><tr><th>category</th><th>root-cause message</th><th class=num>#</th><th>modules</th></tr>")
for (cat, msg), mods in sorted(groups.items(), key=lambda kv:-len(kv[1])):
    out.append(f'<tr><td style="color:{catclass(cat)}">{html.escape(cat)}</td>'
               f'<td><code>{html.escape(msg)}</code></td><td class=num>{len(mods)}</td>'
               f'<td style="font-size:11px">{html.escape(", ".join(sorted(mods)))}</td></tr>')
out.append("</table>")

# ── per-module table ──
out.append("<h2>Per-module results</h2>")
hdr = ["module","primary category","kind","yosys+slang","reader slang","slang&rarr;lg",
       "yosys-slang&rarr;lg","prp&rarr;lg","lec vs slang","lec vs ys-slang","abc","first-fail msg"]
out.append("<table><tr>"+"".join(f"<th>{html.escape(h)}</th>" for h in hdr)+"</tr>")
for r in sorted(rows, key=lambda r:(r["category"]!="PASS", r["category"], r["top"])):
    out.append("<tr>")
    out.append(f'<td><b>{html.escape(r["top"])}</b></td>')
    out.append(f'<td style="color:{catclass(r["category"])};font-weight:600">{html.escape(r["category"])}</td>')
    out.append(f'<td>{html.escape(r["kind"])}</td>')
    for c in ["GATE","RSLANG","LGSLANG","LGYS","PRPGEN","LECSL","LECYS","ABC"]:
        out.append(cell(r[c]))
    out.append(f'<td class=msg title="{html.escape(r["msg"])}">{html.escape(r["msg"][:90])}</td>')
    out.append("</tr>")
out.append("</table>")
open(sys.argv[2],"w").write("\n".join(out))
print(f"PASS {npass}/{n}")
