#!/usr/bin/env bash
# Controls for direct_sweep.py's NON-LEAN surface: probe construction, the
# refusal path, selection, and where scratch lands.
#
# The sweep itself needs the proof slot; these do not, and they are the parts
# that decide whether a sweep that ran is evidence of anything.
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
WORK="$ROOT/generated/master_port/direct_sweep_controls"
CERT="$ROOT/generated/master_port/replay/jobs/CORE-ET/verified_compiler/txfma_e4/lean/txfma_e4_Lgraph.lean"
[ -f "$CERT" ] || { echo "REFUSE: no certificate at $CERT" >&2; exit 2; }
rm -rf "$WORK"; mkdir -p "$WORK"

ROOT="$ROOT" CERT="$CERT" WORK="$WORK" python3 -I - <<'PY'
import importlib.util, os, re, sys
root, cert, work = os.environ["ROOT"], os.environ["CERT"], os.environ["WORK"]
spec = importlib.util.spec_from_file_location(
    "ds", os.path.join(root, "pass", "lean", "scripts", "direct_sweep.py"))
ds = importlib.util.module_from_spec(spec); spec.loader.exec_module(ds)
src = open(cert).read()
fails = []
def ok(name, cond, detail=""):
    print(f"{'ok' if cond else 'MISS'}: {name}{(' -- ' + detail) if detail and not cond else ''}")
    if not cond:
        fails.append(name)

# 1. a real certificate
p = ds.build_probe(src, "txfma_e4", 4)
ok("import_redirected",
   "Compiler.DirectTrace" in p and "Compiler.CompileDesign" not in p)
ok("residual_half_cut", "Compile-and-run" not in p and "compileAndRun" not in p)
head = p[:p.index("open Compiler Compiler.Direct")]
ok("no_dangling_comment",
   not head.rstrip().endswith("-/") and head.count("/-") == head.count("-/"))
ok("probe_appended", "designErrors" in p and "RUNDIRECT" in p)
ok("cycles_substituted", "[0:4]" in p and "@CYCLES@" not in p and "@BASE@" not in p)

# 2. no boundary at all -> a REFUSAL, not `keep everything`
broken = src.replace("/-- Compile-and-run.", "/-- gone.")
broken = re.sub(r"^def (\w+)_step\s*:", r"def \1_stepX :", broken, flags=re.M)
try:
    ds.build_probe(broken, "txfma_e4", 4)
    ok("no_boundary_refused", False, "build_probe returned a probe")
except ValueError as e:
    ok("no_boundary_refused", "cannot locate the residual half" in str(e))

# 3. the `_step` fallback cuts ABOVE the doc comment
fb = src.replace("/-- Compile-and-run.", "/-- gone.")
p2 = ds.build_probe(fb, "txfma_e4", 4)
h2 = p2[:p2.index("open Compiler Compiler.Direct")]
ok("step_fallback_no_dangling_comment",
   "gone." not in p2 and not h2.rstrip().endswith("-/"))

# 4. a refusal is a NAMED ROW, not an exception that kills the worker and
#    takes every other row in flight with it
bad = os.path.join(work, "Broken_Lgraph.lean")
open(bad, "w").write(broken)
row = ds.run_one(bad, work, 4, 5, dict(os.environ))
ok("refusal_is_a_row", row["verdict"] == "PROBE_REFUSED", f"got {row['verdict']}")
ok("refusal_keeps_its_reason", "residual half" in row["reason"])

# 5. a cut that swallowed the certificate is refused too
nocert = src.replace("def txfma_e4_designCert", "def txfma_e4_other")
try:
    ds.build_probe(nocert, "txfma_e4", 4)
    ok("cert_removed_refused", False, "returned a probe with no _designCert")
except ValueError as e:
    ok("cert_removed_refused", "_designCert" in str(e))

sys.exit(1 if fails else 0)
PY
rc=$?

# 6. A SWEEP THAT MATCHED NOTHING IS A CONFIGURATION ERROR, not a vacuous pass.
#    (A --filter regex matched against the PATH instead of the module name made
#    every explicit module set sweep zero certificates and read as success.)
out="$(cd "$ROOT" && python3 -I pass/lean/scripts/direct_sweep.py \
        --out "$WORK/none.tsv" --filter '^definitely_not_a_module$' \
        "$ROOT/generated/master_port/replay/jobs" 2>&1)"; e=$?
if [ "$e" -eq 2 ] && grep -q 'no certificates matched' <<<"$out"; then
  echo "ok: empty_selection_refused -> exit 2"
else
  echo "MISS: empty_selection -> exit $e: $(head -1 <<<"$out")"; rc=1
fi
[ -e "$WORK/none.tsv" ] && { echo "MISS: a refused sweep still wrote its table"; rc=1; }

# 7. the filter matches the MODULE NAME, not the path
out="$(cd "$ROOT" && python3 -I pass/lean/scripts/direct_sweep.py \
        --out "$WORK/one.tsv" --filter '^txfma_e4$' --limit 1 --jobs 1 --timeout 1 \
        "$ROOT/generated/master_port/replay/jobs" 2>&1 </dev/null)"
if grep -q '1 unique certificates' <<<"$out"; then
  echo "ok: filter_matches_module_name -> selected 1"
else
  echo "MISS: filter_matches_module_name: $(head -2 <<<"$out" | tr '\n' ' ')"; rc=1
fi

# 9..12 THE MANIFEST DECIDES THE INPUT, NOT A DIRECTORY WALK.
#
# Pointed at replay/jobs, the sweep found 124 DesignCert files, eight of them
# candidates the static gate REFUSED, and ZERO DINO ones -- the replay's DINO
# jobs emit a legacy GraphCert.  A walk cannot tell an accepted export from a
# refused candidate, and silently omits every configured case with nothing to
# sweep.
cat > "$WORK/man.json" <<EOF
{"to_run": ["$CERT"],
 "investigate_only": ["$ROOT/generated/master_port/replay/jobs/CORE-ET/verified_compiler/tima_adder/lean/tima_adder_Lgraph.lean"],
 "entries": [{"family":"CORE-ET","case":"txfma_e4","mode":"verified_compiler",
              "artifact":"$CERT","artifact_sha256":"deadbeef","aliases":["CORE-ET/legacy/txfma_e4"]}]}
EOF
sel() { (cd "$ROOT" && python3 -I pass/lean/scripts/direct_sweep.py --manifest "$1" \
          ${2:+--manifest-set "$2"} --out "$WORK/sel.tsv" --limit 1 --jobs 1 --timeout 1 2>&1) \
        | sed -n 's/^# \([0-9]*\) unique certificates/\1/p' | head -1; }

n=$(sel "$WORK/man.json"); [ "$n" = "1" ] \
  && echo "ok: manifest_to_run -> 1 payload" \
  || { echo "MISS: manifest_to_run selected '${n:-none}'"; rc=1; }

n=$(sel "$WORK/man.json" investigate_only); [ "$n" = "1" ] \
  && echo "ok: manifest_investigate_only -> its own, separate set" \
  || { echo "MISS: manifest_investigate_only selected '${n:-none}'"; rc=1; }

# The two sets must not be the same payload, or the control proves nothing.
a=$(python3 -I -c "import json;m=json.load(open('$WORK/man.json'));print(m['to_run'][0])")
b=$(python3 -I -c "import json;m=json.load(open('$WORK/man.json'));print(m['investigate_only'][0])")
[ "$a" != "$b" ] && echo "ok: the two manifest sets are distinct payloads" \
                 || { echo "MISS: the control's two sets are the same file"; rc=1; }

python3 -I -c "
import json
m = json.load(open('$WORK/man.json')); m['to_run'] = ['generated/not_here_Lgraph.lean']
json.dump(m, open('$WORK/man_missing.json','w'))"
out="$(cd "$ROOT" && python3 -I pass/lean/scripts/direct_sweep.py --manifest "$WORK/man_missing.json" \
        --out "$WORK/x.tsv" 2>&1)"; e=$?
if [ "$e" -eq 2 ] && grep -q 'not on disk' <<<"$out"; then
  echo "ok: a manifest payload that is not on disk exits 2"
else
  echo "MISS: missing manifest payload -> exit $e"; rc=1
fi
[ -e "$WORK/x.tsv" ] && { echo "MISS: a refused manifest sweep still wrote its table"; rc=1; }

# 13..17 PROVENANCE: the bytes that ran must be the bytes the manifest names.
#
# Measured externally on the committed CLI under pinned Lean: flipping one
# 5-bit source constant from 20 to 19 in an accepted certificate, leaving the
# manifest untouched, returned ACCEPTED 0 and printed the ORIGINAL
# artifact_sha256 in the result row -- a run attributed to source and proof
# evidence that did not describe it.
#
# These call THE TOOL'S OWN RULE, not a copy of it.
ROOT="$ROOT" CERT="$CERT" WORK="$WORK" python3 -I - <<'PYEOF'
import importlib.util, os, hashlib, sys, shutil
root, cert, work = os.environ["ROOT"], os.environ["CERT"], os.environ["WORK"]
spec = importlib.util.spec_from_file_location(
    "ds", os.path.join(root, "pass", "lean", "scripts", "direct_sweep.py"))
ds = importlib.util.module_from_spec(spec); spec.loader.exec_module(ds)
fails = []
def ok(name, cond, detail=""):
    print(f"{'ok' if cond else 'MISS'}: {name}{(' -- ' + detail) if detail and not cond else ''}")
    if not cond: fails.append(name)

good = os.path.join(work, "prov_good.lean")
shutil.copyfile(cert, good)
h = ds.file_sha(good)

ok("prov_valid", ds.provenance_error(good, {
    "artifact_sha256": h, "family": "CORE-ET", "case": "txfma_e4",
    "mode": "verified_compiler"}) == "")

# THE SAME SHAPE AS THE SUPPLIED MUTATION: ONE literal in the certificate
# body changed, manifest untouched.  The supplied example was a 5-bit source
# constant 20 -> 19; this fixture has no such constant, so the first numeric
# literal in a `width :=`/`slot :=`/`mk_bv`/`ofInt` is incremented instead.
# The mutation is ASSERTED TO HAVE APPLIED -- the first version of this control
# substituted a string the fixture does not contain, so the "mutated" file was
# byte-identical and the CLI case failed for the right reason: nothing changed.
import re as _re
mut = os.path.join(work, "prov_mutated.lean")
src = open(good).read()
changed, n = _re.subn(r"(width := )(\d+)",
                      lambda m: m.group(1) + str(int(m.group(2)) + 1), src, count=1)
if n == 0:
    changed, n = _re.subn(r"((?:mk_bv|ofInt) \d+ \()(\d+)(\))",
                          lambda m: m.group(1) + str(int(m.group(2)) + 1) + m.group(3),
                          src, count=1)
open(mut, "w").write(changed)
ok("prov_mutation_is_real", n == 1 and changed != src and ds.file_sha(mut) != h,
   f"the mutation did not apply (n={n})")
e = ds.provenance_error(mut, {"artifact_sha256": h, "family": "CORE-ET",
                              "case": "txfma_e4", "mode": "verified_compiler"})
ok("prov_changed_artifact_refused", "does not match the manifest" in e, e[:70])

ok("prov_no_digest_refused",
   "carries no artifact digest" in ds.provenance_error(good, {
       "family": "CORE-ET", "case": "txfma_e4", "mode": "verified_compiler"}))

sc = os.path.join(work, "prov_side.json")
open(sc, "w").write('{"kind":"lean_design_io"}')
sh = ds.file_sha(sc)
open(sc, "w").write('{"kind":"lean_design_io","changed":true}')
e = ds.provenance_error(good, {"artifact_sha256": h, "sidecar": sc, "sidecar_sha256": sh,
                               "family": "F", "case": "C", "mode": "M"})
ok("prov_changed_sidecar_refused", "sidecar digest" in e, e[:70])

ok("prov_identity_required",
   "carries no mode" in ds.provenance_error(good, {
       "artifact_sha256": h, "family": "CORE-ET", "case": "txfma_e4"}))
sys.exit(1 if fails else 0)
PYEOF
[ $? -ne 0 ] && rc=1

# ...and through the REAL CLI: a mutated payload must produce a named
# non-success row, not an ACCEPTED one.
python3 -I -c "
import json, hashlib, os
good = os.path.join('$WORK', 'prov_good.lean')
h = hashlib.sha256(open(good,'rb').read()).hexdigest()
m = {'to_run': [os.path.join('$WORK','prov_mutated.lean')],
     'entries': [{'family':'CORE-ET','case':'txfma_e4','mode':'verified_compiler',
                  'artifact': os.path.join('$WORK','prov_mutated.lean'),
                  'artifact_sha256': h}]}
json.dump(m, open('$WORK/man_prov.json','w'))"
out="$(cd "$ROOT" && python3 -I pass/lean/scripts/direct_sweep.py --manifest "$WORK/man_prov.json" \
        --out "$WORK/prov.tsv" --jobs 1 --cycles 1 --timeout 60 2>&1)"
if [ -f "$WORK/prov.tsv" ] && grep -q 'PROVENANCE_MISMATCH' "$WORK/prov.tsv"; then
  echo "ok: cli_changed_artifact -> PROVENANCE_MISMATCH row"
else
  echo "MISS: cli_changed_artifact -> $(tail -1 "$WORK/prov.tsv" 2>/dev/null | cut -c1-80)"; rc=1
fi
grep -q 'ACCEPTED' "$WORK/prov.tsv" 2>/dev/null && { echo "MISS: a mutated payload was ACCEPTED"; rc=1; }

# 8. scratch is project-local, never /tmp -- by CALLING the resolver, not by
#    grepping the source for a path.
sd="$(cd "$ROOT" && python3 -I -c "
import importlib.util, os
spec = importlib.util.spec_from_file_location('ds', 'pass/lean/scripts/direct_sweep.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
os.environ.pop('D2SWEEP_TMPDIR', None)
print(m.scratch_dir())")"
case "$sd" in
  "$ROOT"/generated/*) echo "ok: scratch_is_project_local -> ${sd#$ROOT/}" ;;
  /tmp/*|/var/tmp/*)   echo "MISS: scratch defaults to shared temp: $sd"; rc=1 ;;
  *)                   echo "MISS: scratch is outside the project: $sd"; rc=1 ;;
esac
ls -d /tmp/d2sweep_* >/dev/null 2>&1 && { echo "MISS: a probe directory was left in /tmp"; rc=1; }

[ "$rc" -eq 0 ] && echo "PASS: the probe is built from a located boundary, or refused by name" \
                || echo "FAIL: direct_sweep controls missed"
exit $rc
