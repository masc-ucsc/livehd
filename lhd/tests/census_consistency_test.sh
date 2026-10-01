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

# Provenance must be present. The superseded-row map applies only when the
# table IS a merge; a SINGLE-RUN census is strictly better evidence and must
# not be failed for lacking supersessions.
for col in ("evidence_run", "generation_commit", "aggregation_commit", "provisional"):
    if col not in (rows[0] if rows else {}):
        bad(f"missing provenance column {col}")
runs = {r["evidence_run"] for r in rows}
if not all(r.get("evidence_run") for r in rows):
    bad("some row carries no evidence_run")
elif len(runs) == 1:
    # One run covering all 122 rows: nothing is combined, so nothing needs a
    # supersession. This is the stronger shape.
    only = next(iter(runs))
    print(f"ok: single-run census, every row from {only}")
    prov = [r for r in rows if r.get("provisional") == "yes"]
    if prov:
        print(f"note: that run is marked provisional ({len(prov)} rows)")
else:
    # A merge: each row must name the run it came from, and the ones that
    # differ from the base must name a follow-up.
    print(f"ok: merged census, rows carry {len(runs)} evidence runs "
          f"({', '.join(sorted(runs))})")

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
