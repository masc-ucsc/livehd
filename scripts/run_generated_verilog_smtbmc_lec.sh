#!/usr/bin/env bash
set -euo pipefail

# Run a bounded SMTBMC equivalence check between frontend-lowered reference
# Verilog and Verilog regenerated from LiveHD's LGraph.
#
# This is intended for generated CVA6 cache/memory artifacts when monolithic
# `yosys sat -seq N` becomes too large. It keeps all outputs in the caller's
# project-local output directory and records explicit status markers.

usage() {
  cat >&2 <<'EOF'
Usage:
  run_generated_verilog_smtbmc_lec.sh \
    --top TOP \
    --ref reference_pp.v \
    --impl regenerated_from_lgraph.v \
    --depth N \
    --out OUT_DIR \
    [--include DIR]

Environment:
  YOSYS_BIN              Yosys executable. Default: /usr/local/bin/yosys
  YOSYS_SMTBMC_BIN       yosys-smtbmc executable. Default: yosys-smtbmc
  YOSYS_SMTBMC_SOLVER    SMT solver. Default: z3

The proof shape matches the existing SAT BMC convention:
  - miter -equiv -make_assert -ignore_gold_x
  - async2sync; dffunmap
  - zinit -all
  - chformal -assert -skip 1
EOF
}

top=""
ref=""
impl=""
depth=""
out=""
declare -a includes=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --top)
      top="${2:-}"; shift 2 ;;
    --ref)
      ref="${2:-}"; shift 2 ;;
    --impl)
      impl="${2:-}"; shift 2 ;;
    --depth)
      depth="${2:-}"; shift 2 ;;
    --out)
      out="${2:-}"; shift 2 ;;
    --include)
      includes+=("${2:-}"); shift 2 ;;
    -h|--help)
      usage; exit 0 ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ -z "$top" || -z "$ref" || -z "$impl" || -z "$depth" || -z "$out" ]]; then
  usage
  exit 2
fi

case "$depth" in
  ''|*[!0-9]*)
    echo "Depth must be a positive integer: $depth" >&2
    exit 2
    ;;
esac
if [[ "$depth" -le 0 ]]; then
  echo "Depth must be a positive integer: $depth" >&2
  exit 2
fi

if [[ ! -r "$ref" ]]; then
  echo "Missing reference Verilog: $ref" >&2
  exit 2
fi
if [[ ! -r "$impl" ]]; then
  echo "Missing implementation Verilog: $impl" >&2
  exit 2
fi

mkdir -p "$out"
if [[ -e "$out/run.started" || -e "$out/run.finished" || -e "$out/exit_code" ]]; then
  echo "Refusing to overwrite existing SMTBMC run: $out" >&2
  exit 2
fi

yosys_bin="${YOSYS_BIN:-/usr/local/bin/yosys}"
smtbmc_bin="${YOSYS_SMTBMC_BIN:-yosys-smtbmc}"
solver="${YOSYS_SMTBMC_SOLVER:-z3}"

if [[ ! -x "$yosys_bin" ]]; then
  echo "Missing executable YOSYS_BIN=$yosys_bin" >&2
  exit 2
fi
if ! command -v "$smtbmc_bin" >/dev/null 2>&1; then
  echo "Missing yosys-smtbmc executable: $smtbmc_bin" >&2
  exit 2
fi

include_args=()
for dir in "${includes[@]}"; do
  include_args+=("-I$dir")
done

date -Is > "$out/run.started"

set +e
/usr/bin/time -v "$yosys_bin" -p "
  read_verilog -sv -icells ${include_args[*]} $ref;
  hierarchy -top $top;
  proc; bmuxmap; memory; opt; flatten;
  rename -top gold;
  prep -top gold;
  design -stash gold;

  read_verilog -sv -icells ${include_args[*]} $impl;
  hierarchy -top $top;
  proc; bmuxmap; memory; opt; flatten;
  rename -top gate;
  prep -top gate;
  design -stash gate;

  design -copy-from gold -as gold gold;
  design -copy-from gate -as gate gate;
  miter -equiv -make_assert -flatten -make_outputs -ignore_gold_x gold gate miter;
  async2sync;
  dffunmap;
  zinit -all;
  chformal -assert -skip 1;
  hierarchy -top miter;
  write_smt2 -wires $out/miter.smt2
" > "$out/build_smt2.log" 2> "$out/build_smt2.err"
build_status=$?

if [[ "$build_status" -eq 0 ]]; then
  /usr/bin/time -v "$smtbmc_bin" \
    -t "$depth" \
    -s "$solver" \
    -m miter \
    "$out/miter.smt2" \
    > "$out/smtbmc.log" 2> "$out/smtbmc.err"
  status=$?
else
  status=$build_status
fi
set -e

printf '%s\n' "$status" > "$out/exit_code"
date -Is > "$out/run.finished"
exit "$status"
