#!/usr/bin/env bash
# End-to-end check of the plain-proc candidate lowering on the modules whose
# RTLIL SCC it removes: compile -> pass.single_edge -> certificate emission, plus
# an INDEPENDENT defined-input differential against the original RTL.
#
# The `lhd lec` gate is NOT used here, and cannot be: its reference side
# elaborates the RTL through the shipped `proc -ifx`, which is the lowering that
# leaves the cycle, so the solver refuses the REFERENCE ("ref encode failed ...
# WORD-LEVEL CYCLE through: mux_440 -> ..."). `--reader slang` as an alternative
# reference fails too (that frontend cannot elaborate this RTL). The reference
# used instead is verilator reading the original sources -- CORE-ET's own DV
# flow, sharing no lowering with the thing under test.
#
# Usage: scripts/coreet_equiv7.sh <module>...
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
OUTROOT="${OUTROOT:-$ROOT/generated/equiv7}"
VECTORS="${VECTORS:-2000}"
PLAIN="$OUTROOT/read_plain_proc.ys"
# Overridable only so lhd/tests/coreet_equiv7_exit_test.sh can substitute
# stubs and exercise exit propagation without a CORE-ET build.
RUNNER="${RUNNER:-$ROOT/scripts/run_coreet_module_lean.sh}"
DIFFSIM="${DIFFSIM:-$ROOT/scripts/coreet_lowering_diffsim.py}"

mkdir -p "$OUTROOT"
sed 's/^proc -ifx$/proc/' "$ROOT/inou/yosys/inou_yosys_read.ys" > "$PLAIN"
grep -qx 'proc' "$PLAIN" || { echo "FATAL: the -ifx substitution did not apply"; exit 2; }

# EXIT STATUS.  This used to end on a printf and therefore exited 0 whatever
# the table said -- a run whose differential returned 2 reported success to its
# caller, which is how a NOT-MEASURED gate reached a report as a clean phase.
# Any requested module failing generation, candidate emission or the
# differential now makes the whole invocation nonzero, while the per-module
# summary is printed exactly as before.
fail=0
printf '%-16s %-9s %-13s %-12s %s\n' MODULE COMPILE SINGLE_EDGE CERT_EMIT DIFFSIM
for TOP in "$@"; do
  OUT="$OUTROOT/$TOP"; rm -rf "$OUT"; mkdir -p "$OUT"
  YOSYS_SCRIPT="$PLAIN" COREET_TOP="$TOP" OUT="$OUT" LEAN_MODE=verified_compiler \
    RUN_LEAN=false RUN_LEC_GATE=false STOP_AFTER=lean \
    nice -n19 ionice -c3 timeout 3600 "$RUNNER" \
    > "$OUT/runner.log" 2>&1
  rc=$?
  comp=$(grep -oP 'compile exit=\K[0-9]+' "$OUT/runner.log" | head -1)
  se=$(grep -oP 'single_edge exit=\K[0-9]+' "$OUT/runner.log" | head -1)
  cert=$(grep -oP 'lean emit exit=\K[0-9]+' "$OUT/runner.log" | head -1)
  compv="${comp:-?}"; sev="${se:-?}"
  # "cert emitted" is pass.lean EXIT 0 -- certificate TEXT was written. It is
  # NOT lake build / checkDesign / simulator acceptance.
  certv=$([[ "${cert:-1}" == "0" ]] && echo CERT-EMITTED || echo "no(rc=$rc)")

  diff="skipped"
  emit_rc=1
  diff_rc=1
  if [[ "${cert:-1}" == "0" ]]; then
    # STATUS, not file existence: a stale impl_$TOP.v from an earlier run
    # would make a failed emit look like a good one.
    "$LHD" compile "lg:$OUT/lgdb_norm" --top "$TOP" \
      --emit "verilog:$OUT/impl_$TOP.v" --workdir "$OUT/emitv" \
      > "$OUT/emitv.log" 2>&1
    emit_rc=$?
    if [[ "$emit_rc" -ne 0 ]]; then
      diff="emit-failed(rc=$emit_rc)"
    elif [[ ! -r "$OUT/impl_$TOP.v" ]]; then
      diff="no-verilog"
    else
      python3 "$DIFFSIM" --top "$TOP" \
        --impl "$OUT/impl_$TOP.v" --filelist "$ROOT/generated/core-et/filelists/$TOP.f" \
        --out "$OUT/diffsim" --vectors "$VECTORS" > "$OUT/diffsim.log" 2>&1
      diff_rc=$?
      diff="$(grep -oP 'VERDICT: \K.*' "$OUT/diffsim.log" | tail -1)"
      diff="${diff:-no-verdict}"
    fi
  fi
  # `rc` is the runner's own status: a timeout or a failed static gate leaves
  # the stage markers absent, and `${x:-1}` turns that into a failure too.
  if [[ "$rc" -ne 0 || "${comp:-1}" != "0" || "${se:-1}" != "0" \
        || "${cert:-1}" != "0" || "$emit_rc" -ne 0 || "$diff_rc" -ne 0 ]]; then
    fail=1
  fi
  printf '%-16s %-9s %-13s %-12s %s\n' "$TOP" "$compv" "$sev" "$certv" "$diff"
done

exit "$fail"
