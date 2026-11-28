#!/usr/bin/env bash
# run_tier.sh quick|full: the Lean tools' tests, by tier, in one run.
#
#   quick  every commit (about 30 min): .build-proj.sh (every module, DirectTests' guards, the axiom
#          gate), the Lean-only checks (image_check: program images; engine_examples: every engine
#          on lgraph-sim's in-Lean examples, and the ir route's refusal of the malformed ones), the
#          certificate format (dcert_version_check: the codec on every version; cert_admit_selftest.sh:
#          every driver admits DCERT4 and refuses the rest, on certificates built from the committed
#          fixtures/ram12.dcert), the drivers that need no lhd (quick_selftest.sh: dcert2_memroute's
#          self-test and fixtures, lgraph-sim's command line), the operator corpus on every engine
#          against ref (opcorpus_test.sh, interpreted), reach_selftest and judge_selftest.
#   full   nightly (about 3 h): quick, then the sym, refmap, run, ckpt and lhd_ckpt_import
#          self-tests, xpatch, the lake side (CompileOpt, LgraphSim), the census traces, the
#          Verilog designs against Verilator on every route (vref_test.sh, with TV and legacy), and
#          the operator corpus natively on every route and TV (opcorpus_test.sh).
#
# One line per test, then a summary:
#   PASS <test> (<seconds> s)
#   FAIL <test> (<what failed>; log <file>)
#   SKIP <test> (<why>)
# The exit code is 0 only when every test passed. A SKIP is counted as a failure under STRICT=1,
# which a CI sets, so nothing passes by not running.
#
# Environment:
#   OUT          results folder (default generated/run_tier/<tier>)
#   LHD          an lhd that writes `memories` into _io.json (default bazel-bin/lhd/lhd)
#   BUILD=0      reuse formal/lean/.olean-dev instead of rebuilding it
#   STRICT=1     a SKIP fails the run
#   CENSUS_ROWS  folder of census rows (<family>/rows/<design>/{lean/*.dcert,stim.txt}) and
#   STAGE0       folder of their reference traces (out_<design>_<route>_32.txt), for the census
#                traces (full); without them the test is a SKIP
#   NATROOT      build-opt.sh ROOT for the native cert_trace and per-design simulators of the census
#                traces, vref_test and opcorpus_test (default $OUT/natroot)
#   verilator on PATH for vref_test (full); without it the test is a SKIP
set -uo pipefail
TIER="${1:-}"
[[ "$TIER" == quick || "$TIER" == full ]] || { echo "usage: run_tier.sh quick|full" >&2; exit 2; }
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
L="$ROOT/formal/lean"
OUT="${OUT:-$ROOT/generated/run_tier/$TIER}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
STRICT="${STRICT:-0}"
NATROOT="${NATROOT:-$OUT/natroot}"
mkdir -p "$OUT"
SUMMARY="$OUT/summary.tsv"
printf 'test\tresult\tseconds\tnote\n' > "$SUMMARY"
npass=0; nfail=0; nskip=0

record() { # <test> <PASS|FAIL|SKIP> <seconds> <note>
  printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" >> "$SUMMARY"
  case "$2" in
    PASS) npass=$((npass + 1)); echo "PASS $1 ($3 s)" ;;
    FAIL) nfail=$((nfail + 1)); echo "FAIL $1 ($4)" ;;
    SKIP) nskip=$((nskip + 1)); echo "SKIP $1 ($4)" ;;
  esac
}

# run_test <name> <ok-pattern or -> <command...>: PASS when the command exits 0 and, unless the
# pattern is `-`, its log has a line matching the pattern
run_test() {
  local name=$1 pat=$2; shift 2
  local log="$OUT/$name.log" t0=$SECONDS rc
  "$@" > "$log" 2>&1; rc=$?
  local dt=$((SECONDS - t0))
  if [[ $rc -ne 0 ]]; then
    record "$name" FAIL "$dt" "exit $rc: $(grep -m1 -E 'FAIL|error|Error' "$log" | cut -c1-120); log $log"
  elif [[ "$pat" != - ]] && ! grep -qE "$pat" "$log"; then
    record "$name" FAIL "$dt" "no line matching '$pat'; log $log"
  else
    record "$name" PASS "$dt" ""
  fi
}

lean_check() { # <check>: a tests/<check>.lean that prints PASS ...
  (cd "$L" && LEAN_PATH="$PWD/.olean-dev" lean --run "$SCRIPT_DIR/$1.lean" "${@:2}")
}

echo "run_tier $TIER at $(cd "$ROOT" && git rev-parse --short HEAD) ($(cd "$ROOT" && git status --porcelain | grep -vc '^??') files changed)"

# ---- quick
if [[ "${BUILD:-1}" != 0 ]]; then
  build_proj() { (cd "$L" && bash .build-proj.sh) && ! grep -q ' FAIL ' "$OUT/build_proj.log"; }
  run_test build_proj - build_proj
else
  record build_proj SKIP 0 "BUILD=0: formal/lean/.olean-dev reused"
fi
for c in native_symbolic fp_symbolic mem_addr_check getmask_fast_check wide_fast_check residual_opt_check \
         residual_inc_check stim_tab_check ckpt_check image_check; do
  run_test "$c" '^PASS' lean_check "$c"
done
run_test engine_examples '^PASS engine: [1-9][0-9]* runs$' lean_check engine_check 64 --examples
run_test quick_selftest '^PASS quick_selftest' env OUT="$OUT/out_quick" bash "$SCRIPT_DIR/quick_selftest.sh"
run_test dcert_version_check '^PASS dcert_version_check' lean_check dcert_version_check "$SCRIPT_DIR/fixtures/ram12.dcert"
run_test cert_admit_selftest '^PASS cert_admit_selftest' env OUT="$OUT/out_cert_admit" bash "$SCRIPT_DIR/cert_admit_selftest.sh"
run_test opcorpus '^PASS opcorpus_test' env OUT="$OUT/out_opcorpus" bash "$SCRIPT_DIR/opcorpus_test.sh"
if [[ -x "$LHD" ]]; then
  run_test reach_selftest '^PASS reach_selftest' env LHD="$LHD" OUT="$OUT/out_reach" bash "$SCRIPT_DIR/reach_selftest.sh"
  run_test judge_selftest 'judge self-test: all passed' env LHD="$LHD" OUT="$OUT/out_judge" bash "$SCRIPT_DIR/judge_selftest.sh"
else
  record reach_selftest SKIP 0 "no lhd at $LHD"
  record judge_selftest SKIP 0 "no lhd at $LHD"
fi

# ---- full
if [[ "$TIER" == full ]]; then
  if [[ -x "$LHD" ]]; then
    run_test sym_selftest '^PASS: sym_selftest' env LHD="$LHD" OUT="$OUT/out_sym" bash "$SCRIPT_DIR/sym_selftest.sh"
    run_test refmap_selftest '^PASS: refmap_selftest' env LHD="$LHD" OUT="$OUT/out_refmap" bash "$SCRIPT_DIR/refmap_selftest.sh"
    run_test run_selftest '^PASS run_selftest' env LHD="$LHD" OUT="$OUT/out_run" bash "$SCRIPT_DIR/run_selftest.sh"
    run_test ckpt_selftest '^PASS ckpt_selftest' env LHD="$LHD" OUT="$OUT/out_ckpt" bash "$SCRIPT_DIR/ckpt_selftest.sh"
    run_test lhd_ckpt_import_test '^PASS lhd_ckpt_import_test' env LHD="$LHD" OUT="$OUT/out_import" \
      bash "$SCRIPT_DIR/lhd_ckpt_import_test.sh"
    if command -v cadical > /dev/null; then
      run_test xpatch - env LHD="$LHD" OUT="$OUT/out_xpatch" bash "$ROOT/pass/lean/xpatch/run.sh"
    else
      record xpatch SKIP 0 "cadical is not on PATH"
    fi
  else
    for t in sym_selftest refmap_selftest run_selftest ckpt_selftest lhd_ckpt_import_test xpatch; do
      record "$t" SKIP 0 "no lhd at $LHD"
    done
  fi
  run_test lake - bash -c "cd '$L' && lake build LeanSemanticPrimitives.Compiler.CompileOpt LgraphSim"
  if [[ -n "${CENSUS_ROWS:-}" && -n "${STAGE0:-}" ]]; then
    census_traces() {
      local nr="$NATROOT" bad=0 n=0
      (cd "$L" && OPT=-O2 ROOT="$nr" bash Futamura_Projection_Scripts/build-opt.sh \
        Futamura_Projection_Scripts/cert_trace.lean) || return 1
      for d in "$CENSUS_ROWS"/*/rows/*; do
        local r; r=$(basename "$d")
        local cert; cert=$(ls "$d"/lean/*.dcert)
        for rt in ir vc; do
          [[ -f "$STAGE0/out_${r}_${rt}_32.txt" ]] || continue
          n=$((n + 1))
          bash -c "ulimit -s unlimited; '$nr/nat/cert_trace' '$cert' 32 '$d/stim.txt' edges:third route:$rt" \
            > "$OUT/census_${r}_$rt.txt" 2>&1
          if cmp -s <(grep '^STEP' "$OUT/census_${r}_$rt.txt") <(grep '^STEP' "$STAGE0/out_${r}_${rt}_32.txt"); then
            echo "same $r $rt"
          else echo "DIFFERS $r $rt"; bad=$((bad + 1)); fi
        done
      done
      echo "census traces: $n compared, $bad differ"
      [[ $n -gt 0 && $bad -eq 0 ]]
    }
    run_test census_traces 'census traces: [1-9][0-9]* compared, 0 differ' census_traces
  else
    record census_traces SKIP 0 "CENSUS_ROWS and STAGE0 are not set"
  fi
  run_test opcorpus_native '^PASS opcorpus_test' env OUT="$OUT/out_opcorpus_native" NATROOT="$NATROOT" NATIVE=1 \
    PERDESIGN=1 ENGINE_CHECK=0 bash "$SCRIPT_DIR/opcorpus_test.sh"
  if ! command -v verilator > /dev/null; then
    record vref_test SKIP 0 "verilator is not on PATH"
  elif [[ ! -x "$LHD" ]]; then
    record vref_test SKIP 0 "no lhd at $LHD"
  else
    run_test vref_test '^PASS vref_test' env LHD="$LHD" OUT="$OUT/out_vref" NATROOT="$NATROOT" PERDESIGN=1 \
      bash "$SCRIPT_DIR/vref_test.sh"
  fi
fi

echo "summary: $npass passed, $nfail failed, $nskip skipped (results $SUMMARY)"
[[ $nfail -eq 0 && ( "$STRICT" != 1 || $nskip -eq 0 ) ]]
