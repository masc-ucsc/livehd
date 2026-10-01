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

mkdir -p "$OUTROOT"
sed 's/^proc -ifx$/proc/' "$ROOT/inou/yosys/inou_yosys_read.ys" > "$PLAIN"
grep -qx 'proc' "$PLAIN" || { echo "FATAL: the -ifx substitution did not apply"; exit 2; }

printf '%-16s %-9s %-13s %-12s %s\n' MODULE COMPILE SINGLE_EDGE CERT_EMIT DIFFSIM
for TOP in "$@"; do
  OUT="$OUTROOT/$TOP"; rm -rf "$OUT"; mkdir -p "$OUT"
  YOSYS_SCRIPT="$PLAIN" COREET_TOP="$TOP" OUT="$OUT" LEAN_MODE=verified_compiler \
    RUN_LEAN=false RUN_LEC_GATE=false STOP_AFTER=lean \
    nice -n19 ionice -c3 timeout 3600 "$ROOT/scripts/run_coreet_module_lean.sh" \
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
  if [[ "${cert:-1}" == "0" ]]; then
    "$LHD" compile "lg:$OUT/lgdb_norm" --top "$TOP" \
      --emit "verilog:$OUT/impl_$TOP.v" --workdir "$OUT/emitv" \
      > "$OUT/emitv.log" 2>&1
    if [[ -r "$OUT/impl_$TOP.v" ]]; then
      python3 "$ROOT/scripts/coreet_lowering_diffsim.py" --top "$TOP" \
        --impl "$OUT/impl_$TOP.v" --filelist "$ROOT/generated/core-et/filelists/$TOP.f" \
        --out "$OUT/diffsim" --vectors "$VECTORS" > "$OUT/diffsim.log" 2>&1
      diff="$(grep -oP 'VERDICT: \K.*' "$OUT/diffsim.log" | tail -1)"
      diff="${diff:-no-verdict}"
    else
      diff="no-verilog"
    fi
  fi
  printf '%-16s %-9s %-13s %-12s %s\n' "$TOP" "$compv" "$sev" "$certv" "$diff"
done
