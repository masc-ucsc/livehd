#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The committed CORE-ET census must agree with the number it is cited under.
#
# It did not, once: pass/lean/CENSUS_D2_122.tsv was published alongside a
# "105 of 122" claim while still being the provisional 102-row baseline --
# `txfma_f1` recorded compile=143 (a SIGTERM from a mis-targeted kill) and
# `null_vpu` / `minion_dcache_texsend` recorded static-gates-failed (a gate
# defect). A reader checking the primary data would have found it contradicted
# the summary.
#
# So: the adjudicated table is checked against its own claims, every accepted
# row must carry the evidence for being accepted, and the superseded rows must
# point at the follow-up runs they came from.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." 2>/dev/null && pwd)"
TSV="${CENSUS_TSV:-}"
[ -n "$TSV" ] || for c in "${TEST_SRCDIR:-}/_main/pass/lean/CENSUS_D2_122.tsv" \
                          "$ROOT/pass/lean/CENSUS_D2_122.tsv" \
                          "pass/lean/CENSUS_D2_122.tsv"; do
  [ -r "$c" ] && { TSV="$c"; break; }
done
[ -n "$TSV" ] || { echo "FAIL: cannot find CENSUS_D2_122.tsv"; exit 1; }
echo "checking $TSV"

CENSUS_TSV="$TSV" EXPECT_MODULES="${EXPECT_MODULES:-122}" EXPECT_ACCEPTED="${EXPECT_ACCEPTED:-105}" \
CYCLES="${CYCLES:-4}" python3 - <<'PYCHK'
import csv, os, sys
tsv = os.environ["CENSUS_TSV"]
want_n = int(os.environ["EXPECT_MODULES"]); want_acc = int(os.environ["EXPECT_ACCEPTED"])
cycles = os.environ["CYCLES"]
rows = list(csv.DictReader(open(tsv, newline=""), delimiter="\t"))
fails = 0

def bad(msg):
    global fails
    print(f"FAIL: {msg}"); fails += 1

mods = [r["module"] for r in rows]
if len(rows) != want_n:
    bad(f"{len(rows)} rows, expected {want_n}")
else:
    print(f"ok: {len(rows)} rows")
if len(set(mods)) != len(mods):
    dup = sorted({m for m in mods if mods.count(m) > 1})
    bad(f"duplicate module rows: {','.join(dup)}")
else:
    print("ok: every module appears exactly once")

acc = [r for r in rows if r["stage"] == "accepted"]
if len(acc) != want_acc:
    bad(f"{len(acc)} accepted, expected {want_acc}")
else:
    print(f"ok: {len(acc)} accepted")

# An `accepted` row must CARRY the evidence for being accepted.
for r in acc:
    if r.get("checkdesign") != "ACCEPTED":
        bad(f"{r['module']} is accepted but checkdesign={r.get('checkdesign')!r}")
    if str(r.get("direct_cycles")) != cycles:
        bad(f"{r['module']} is accepted but direct_cycles={r.get('direct_cycles')!r}, expected {cycles}")
    for c in ("compile", "single_edge", "emit", "static_gates"):
        if r.get(c) != "0":
            bad(f"{r['module']} is accepted but {c}={r.get(c)!r}")
print("ok: every accepted row carries checkDesign=ACCEPTED, the requested cycles, and clean gates")

# Provenance must be present, and the superseded rows must name a follow-up run.
for col in ("evidence_run", "generation_commit", "aggregation_commit", "provisional"):
    if col not in (rows[0] if rows else {}):
        bad(f"missing provenance column {col}")
SUPERSEDED = {"txfma_f1": "recovery_kill143", "core_top": "recovery_kill143",
              "minion_dcache_top": "recovery_kill143", "minion_top": "recovery_kill143",
              "null_vpu": "zeronode_validation", "minion_dcache_texsend": "zeronode_validation"}
by = {r["module"]: r for r in rows}
for mod, run in SUPERSEDED.items():
    r = by.get(mod)
    if r is None:
        bad(f"{mod} is missing from the census entirely")
    elif r.get("evidence_run") != run:
        bad(f"{mod} should come from {run}, but says evidence_run={r.get('evidence_run')!r}")
print("ok: the superseded rows point at their follow-up runs")

runs = {r["evidence_run"] for r in rows}
if len(runs) < 2:
    bad("every row claims one run: 105 is COMBINED evidence and must say so per row")
else:
    print(f"ok: rows carry {len(runs)} distinct evidence runs ({', '.join(sorted(runs))})")

# No machine-absolute paths, no trailing whitespace.
for i, line in enumerate(open(tsv), 1):
    if "/mada/" in line or "/soe/" in line:
        bad(f"line {i} carries a machine-absolute path"); break
for i, line in enumerate(open(tsv), 1):
    if line.rstrip("\n") != line.rstrip():
        bad(f"line {i} has trailing whitespace (git show --check will flag it)"); break
print("ok: no absolute paths, no trailing whitespace")
sys.exit(1 if fails else 0)
PYCHK
rc=$?
[ "$rc" -eq 0 ] || { echo "FAIL: the committed census contradicts its own summary"; exit 1; }
echo "PASS: census_consistency_test"
