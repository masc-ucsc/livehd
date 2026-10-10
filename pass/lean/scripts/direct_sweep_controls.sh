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
