#!/usr/bin/env bash
# Controls for the CVA6 wrapper-dependency wiring.
#
# What this guards. `legacy_cva6_alu_export` failed in BOTH configured modes --
# "unknown package cva6_alu_export_pkg", "undeclared identifier Cfg" -- because
# the runner appended only CVA6_WRAPPER_FILE and the generated CVA6 filelist
# does not contain packages we wrote beside our wrappers. The repair declares
# dependencies explicitly in scripts/cva6_module_wrappers/WRAPPER_DEPS.tsv and
# appends them, in order, ahead of the wrapper.
#
# Three things have to hold, and each is checked against the ACTUAL runner and
# the ACTUAL orchestrator rather than a restatement of them:
#
#   1. the dependency lands IMMEDIATELY BEFORE the wrapper, not merely
#      somewhere in the file -- elaboration order is the contract;
#   2. a declared dependency that is missing is a REFUSAL, not a quiet pass,
#      at the runner (exit 2) and at the orchestrator (unresolved prerequisite);
#   3. a wrapper with no declared dependency is unaffected.
#
# No lhd is invoked: LHD is pointed at /bin/true, so this exercises filelist
# assembly only and makes no typecheck, proof or timing claim.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$ROOT/generated/wrapper_deps_controls"
rm -rf "$TMP"; mkdir -p "$TMP"
W="$ROOT/scripts/cva6_module_wrappers"
FL="$ROOT/generated/cva6_filelists/cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f"
fail=0
note() { printf '  %-5s %s\n' "$1" "$2"; [[ "$1" == ok ]] || fail=1; }

if [[ ! -r "$FL" ]]; then
  echo "SKIP: no CVA6 filelist at $FL -- unresolved prerequisite, not a pass" >&2
  exit 3
fi

run_assembly() { # $1 = OUT dir, $2 = CVA6_WRAPPER_DEPS
  OUT="$1" CVA6_TOP=cva6_alu_export CVA6_BENDER_TOP=cva6_alu_export \
    CVA6_FILELIST="$FL" CVA6_WRAPPER_FILE="$W/cva6_alu_export.sv" \
    CVA6_WRAPPER_DEPS="$2" LHD=/bin/true RUN_LEAN=false \
    CVA6_ROOT="${CVA6_ROOT:-/mada/users/czeng14/projects/cva6}" \
    bash "$ROOT/scripts/run_cva6_module_lean_stress.sh" > "$1/run.log" 2>&1
}

# 1. ORDER.
mkdir -p "$TMP/ordered"
set +e; run_assembly "$TMP/ordered" "$W/cva6_alu_export_pkg.sv"; set -e
f="$TMP/ordered/generated_inputs/cv64a6_imafdc_sv39_hpdcache_wb_cva6_alu_export.f"
if [[ -r "$f" ]]; then
  dep=$(grep -n "cva6_alu_export_pkg.sv$" "$f" | cut -d: -f1)
  wrp=$(grep -n "cva6_module_wrappers/cva6_alu_export.sv$" "$f" | cut -d: -f1)
  if [[ -n "$dep" && -n "$wrp" && $((wrp - dep)) -eq 1 ]]; then
    note ok "the package is line $dep and the wrapper line $wrp -- immediately before"
  else
    note FAIL "dependency/wrapper lines are '$dep'/'$wrp', not adjacent in that order"
  fi
else
  note FAIL "no filelist was assembled at $f"
fi

# 2a. A MISSING DECLARED DEPENDENCY IS REFUSED BY THE RUNNER.
mkdir -p "$TMP/missing"
set +e; run_assembly "$TMP/missing" "$W/not_a_real_package.sv"; rc=$?; set -e
if [[ $rc -eq 2 ]] && grep -q "missing CVA6_WRAPPER_DEPS entry" "$TMP/missing/run.log"; then
  note ok "runner refuses a missing declared dependency (exit 2, named)"
else
  note FAIL "runner exit $rc on a missing declared dependency; expected 2 with a named FATAL"
fi

# 2b. AND BY THE ORCHESTRATOR, which must not reach the runner at all.
python3 -I - "$ROOT" <<'PY' || fail=1
import importlib.util, sys, pathlib
root = pathlib.Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("ro", root / "scripts" / "replay_orchestrator.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
tsv, orig = m.WRAPPER_DEPS_TSV, m.WRAPPER_DEPS_TSV.read_text()
try:
    tsv.write_text(orig + "cva6_tlb_gate.sv\tnot_a_real_package.sv\n")
    got = m.wrapper_deps(root / "scripts/cva6_module_wrappers/cva6_tlb_gate.sv")
    assert got == ([], "not_a_real_package.sv"), got
    print("  ok    orchestrator reports the absent dependency by name")
    tsv.write_text(orig + "cva6_ras_gate.sv\t\n")
    try:
        m._wrapper_dep_table()
        print("  FAIL  a row with no dependencies was accepted"); raise SystemExit(1)
    except SystemExit as e:
        if "no dependencies" not in str(e):
            raise
        print("  ok    a row declaring no dependencies is a malformed table")
finally:
    tsv.write_text(orig)
PY

# 3. A WRAPPER WITH NO DECLARED DEPENDENCY IS UNAFFECTED.
python3 -I - "$ROOT" <<'PY' || fail=1
import importlib.util, sys, pathlib
root = pathlib.Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("ro", root / "scripts" / "replay_orchestrator.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
deps, missing = m.wrapper_deps(root / "scripts/cva6_module_wrappers/cva6_tlb_gate.sv")
assert (deps, missing) == ([], None), (deps, missing)
print("  ok    an undeclared wrapper resolves to no dependencies")
PY

if [[ $fail -eq 0 ]]; then echo "WRAPPER-DEPS CONTROLS OK"; else echo "WRAPPER-DEPS CONTROLS FAILED"; fi
exit $fail
