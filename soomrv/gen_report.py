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

# cross-check counters (independent of primary verdict)
cc = {
  "yosys+slang fails (gate)":        sum(1 for r in rows if r["GATE"]=="FAIL"),
  "--reader slang fails":            cnt_primary("--reader slang fails"),
  "lg gen from slang fails":         cnt_primary("lg gen from slang fails"),
  "lg gen from yosys-slang fails":   sum(1 for r in rows if r["LGYS"]=="FAIL"),
  "prp gen fails":                   cnt_primary("prp gen fails"),
  "lec prp vs from slang fails":     sum(1 for r in rows if r["LECSL"]=="REFUTED"),
  "lec prp vs from yosys-slang fails":sum(1 for r in rows if r["LECYS"]=="REFUTED"),
  "abc gen fails":                   sum(1 for r in rows if r["ABC"]=="FAIL"),
  "lec inconclusive (timeout)":      cnt_primary("lec inconclusive"),
  "other":                           sum(v for k,v in prim.items() if k.startswith("other")),
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

# ── counters ──
out.append("<h2>Failure-point counters</h2>")
out.append("<table><tr><th>category</th><th class=num>count</th><th>bar</th></tr>")
out.append(f'<tr><td style="color:#1a7f37;font-weight:700">PASS</td><td class=num>{npass}</td>'
           f'<td><span class=bar style="width:{npass*6}px"></span></td></tr>')
for k,v in cc.items():
    out.append(f'<tr><td>{html.escape(k)}</td><td class=num>{v}</td>'
               f'<td><span class=bar style="width:{v*6}px;background:#cf222e"></span></td></tr>')
out.append("</table>")
out.append("<p style='font-size:12px;color:#57606a'>PASS + the primary buckets "
           "(--reader slang / lg gen from slang / prp gen / lec vs slang / lec inconclusive / "
           "yosys+slang) partition the modules. The yosys-slang lg-gen, lec-vs-yosys-slang and "
           "abc counters are <i>cross-checks</i> &mdash; a PASS module may still fail one.</p>")

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
