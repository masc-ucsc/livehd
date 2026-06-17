#!/usr/bin/env python3
"""reclassify.py <results.tsv> — refine the yosys+slang bucket per-FILE.

The gate reads the WHOLE single-unit design, so a yosys-slang parse error in ANY
file fails the gate for EVERY module.  A module belongs in "yosys+slang fails"
only when its OWN defining file is one yosys-slang cannot read; otherwise its
slang-reader failure is a fixable LiveHD `--reader slang` gap.

Computes the yosys-slang-unparseable file set as the UNION of error spans across
all per-module gate logs (/tmp/sv2_<M>/gate.log; slang stops early, so a single
read is incomplete), writes soomrv/ys_bad_files.txt, then moves
"yosys+slang fails" -> "--reader slang fails" when the module's file is clean,
rewriting the .md header to match.  Idempotent.
"""
import sys, re, glob, os, collections

HERE = os.path.dirname(os.path.abspath(__file__))
SRC  = "/mada/users/renau/projs/soomrv/repo"
tsv  = sys.argv[1]

# yosys-slang-unparseable files: union of error spans across gate logs
bad = set()
for gl in glob.glob("/tmp/sv2_*/gate.log"):
    for line in open(gl, errors="ignore"):
        m = re.search(r"((?:src/(?:lib/)?|hardfloat/)[A-Za-z0-9_]+\.s?v):\d+:\d+: error", line)
        if m:
            bad.add(m.group(1))
open(os.path.join(HERE, "ys_bad_files.txt"), "w").write("\n".join(sorted(bad)) + "\n")

# module -> defining file
m2f = {}
for f in glob.glob(SRC + "/src/*.sv") + glob.glob(SRC + "/src/lib/*.sv"):
    rel = os.path.relpath(f, SRC)
    nc = re.sub(r"//[^\n]*", "", open(f).read()); nc = re.sub(r"/\*.*?\*/", "", nc, flags=re.S)
    for mm in re.finditer(r"\bmodule\s+(\w+)", nc):
        m2f.setdefault(mm.group(1), rel)
open(os.path.join(HERE, "module_file.txt"), "w").write(
    "\n".join(f"{k} {v}" for k, v in sorted(m2f.items())) + "\n")

rows = [l.rstrip("\n").split("\t") for l in open(tsv) if l.strip()]
moved = 0
for r in rows:
    while len(r) < 12: r.append("")
    top, cat = r[0], r[1]
    if cat == "yosys+slang fails" and m2f.get(top, "?") not in bad:
        r[1] = "--reader slang fails"
        moved += 1
        md = os.path.join(HERE, "fail", top + ".md")
        if os.path.exists(md):
            txt = open(md).read().replace("# " + top + " — yosys+slang fails",
                                          "# " + top + " — --reader slang fails (own file is yosys-slang-parseable)")
            open(md, "w").write(txt)
open(tsv, "w").write("\n".join("\t".join(r) for r in rows) + "\n")
print(f"reclassify: {len(bad)} yosys-slang-bad files; moved {moved} yosys+slang->--reader slang")
