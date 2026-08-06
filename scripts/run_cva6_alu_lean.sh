#!/usr/bin/env bash
set -euo pipefail

# CVA6 ALU -> pass.lean runner (explicit file list, slang front-end).
#
# Why this is a separate script from run_cva6_module_lean_stress.sh: the ALU route
# does NOT go through a Bender filelist.  It needs a hand-ordered set of packages
# plus a one-line instantiation top (scripts/cva6_module_wrappers/cva6_alu_export.sv),
# and it needs CVA6's `alu` module elaborated with CONCRETE parameter defaults --
# a bare `alu` leaves CVA6Cfg/fu_data_t at their empty/`logic` defaults, which
# yields degenerate 1-bit ports (see scripts/CVA6_SV2V_FILELIST_REFERENCE.md #4).
#
# Rather than vendor a modified copy of core/alu.sv (413 lines that would silently
# drift from upstream), we DERIVE alu_concrete.sv from the real alu.sv with a
# two-line parameter-default patch and hard-fail if the patch does not apply.
#
# Reference: this reproduces the recipe that produced the 5638-node
# cva6_alu_export_Lgraph.thy in generated/cva6_isabelle_20260607/, retargeted at
# pass.lean with the step-5 fast-view bridge.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIVEHD_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
WRAPPERS="$SCRIPT_DIR/cva6_module_wrappers"

CVA6_ROOT="${CVA6_ROOT:-/mada/users/czeng14/projects/cva6-clean/cva6}"
# The ALU export pkg reads cva6_config_pkg::cva6_cfg, so the config package
# compiled here selects the configuration.  sv39 (not sv32): see the reference doc.
CONFIG_PKG="${CVA6_CONFIG_PKG:-$CVA6_ROOT/core/include/cv64a6_imafdc_sv39_config_pkg.sv}"
TOP="cva6_alu_export"
LHD="${LHD:-$LIVEHD_ROOT/bazel-bin/lhd/lhd}"
LAKE="${LAKE:-lake}"

RUN_LEAN="${RUN_LEAN:-false}"
EMIT_CERT="${LEAN_EMIT_CERT:-true}"
EMIT_FAST_BRIDGE="${LEAN_EMIT_FAST_BRIDGE:-false}"
CERT_WF="${LEAN_CERT_WF:-skip}"
MAX_WIDTH="${LEAN_MAX_WIDTH:-1048576}"
LEAN_JOBS="${LEAN_JOBS:-8}"
LEAN_CPUSET="${LEAN_CPUSET:-0-7}"

OUT="${OUT:-$LIVEHD_ROOT/generated/cva6_alu_lean}"
LOG_DIR="$OUT/logs"
WORK_DIR="$OUT/lhd_work"
LG_DIR="$OUT/lgdb"
LEAN_DIR="$OUT/lean"
GEN_DIR="$OUT/generated_inputs"
RUNTIME_TMP_DIR="$OUT/runtime_tmp"

mkdir -p "$LOG_DIR" "$WORK_DIR" "$LG_DIR" "$LEAN_DIR" "$GEN_DIR" "$RUNTIME_TMP_DIR"
export TMPDIR="$RUNTIME_TMP_DIR"
export TMP="$RUNTIME_TMP_DIR"
export TEMP="$RUNTIME_TMP_DIR"

[[ -x "$LHD" ]] || { echo "FATAL: missing lhd binary: $LHD (bazel build //lhd:lhd)" >&2; exit 2; }
[[ -r "$CONFIG_PKG" ]] || { echo "FATAL: missing config pkg: $CONFIG_PKG" >&2; exit 2; }

if [[ "$EMIT_FAST_BRIDGE" == "true" && "$EMIT_CERT" != "true" ]]; then
  echo "FATAL: LEAN_EMIT_FAST_BRIDGE=true requires LEAN_EMIT_CERT=true" >&2
  exit 2
fi

# ---------------------------------------------------------------------------
# Derive alu_concrete.sv from upstream core/alu.sv (checked 2-line patch).
# ---------------------------------------------------------------------------
ALU_SRC="$CVA6_ROOT/core/alu.sv"
ALU_CONCRETE="$GEN_DIR/alu_concrete.sv"
[[ -r "$ALU_SRC" ]] || { echo "FATAL: missing $ALU_SRC" >&2; exit 2; }

sed -e 's|parameter config_pkg::cva6_cfg_t CVA6Cfg = config_pkg::cva6_cfg_empty,|parameter config_pkg::cva6_cfg_t CVA6Cfg = cva6_alu_export_pkg::Cfg,|' \
    -e 's|parameter type fu_data_t = logic$|parameter type fu_data_t = cva6_alu_export_pkg::export_fu_data_t|' \
    "$ALU_SRC" > "$ALU_CONCRETE"

# Fail loudly if upstream alu.sv changed shape: both substitutions must have fired,
# and no empty-config default may survive.  A silent no-op here would produce a
# degenerate 1-bit-port model that still "compiles".
if ! grep -q 'CVA6Cfg = cva6_alu_export_pkg::Cfg,' "$ALU_CONCRETE" \
   || ! grep -q 'fu_data_t = cva6_alu_export_pkg::export_fu_data_t' "$ALU_CONCRETE" \
   || grep -q 'cva6_cfg_empty' "$ALU_CONCRETE"; then
  echo "FATAL: the alu.sv parameter-default patch did not apply cleanly." >&2
  echo "       Upstream $ALU_SRC changed; update the sed patterns in $0." >&2
  exit 2
fi

# ---------------------------------------------------------------------------
# File list, in dependency order (packages first, then leaf cells, then the DUT
# and the instantiation top).  Mirrors the recipe that produced the 5638-node
# Isabelle model.
# ---------------------------------------------------------------------------
FILES=(
  "$CVA6_ROOT/core/include/config_pkg.sv"
  "$CONFIG_PKG"
  "$CVA6_ROOT/core/include/riscv_pkg.sv"
  "$CVA6_ROOT/core/include/build_config_pkg.sv"
  "$CVA6_ROOT/core/include/ariane_pkg.sv"
  "$WRAPPERS/cva6_alu_export_pkg.sv"
  "$CVA6_ROOT/vendor/pulp-platform/common_cells/src/cf_math_pkg.sv"
  "$CVA6_ROOT/vendor/pulp-platform/common_cells/src/popcount.sv"
  "$CVA6_ROOT/vendor/pulp-platform/common_cells/src/lzc.sv"
  "$ALU_CONCRETE"
  "$WRAPPERS/cva6_alu_export.sv"
)
for f in "${FILES[@]}"; do
  [[ -r "$f" ]] || { echo "FATAL: missing source: $f" >&2; exit 2; }
done

{
  echo "CVA6_ROOT=$CVA6_ROOT"
  echo "CONFIG_PKG=$CONFIG_PKG"
  echo "TOP=$TOP"
  echo "OUT=$OUT"
  echo "RUN_LEAN=$RUN_LEAN"
  echo "LEAN_EMIT_CERT=$EMIT_CERT"
  echo "LEAN_EMIT_FAST_BRIDGE=$EMIT_FAST_BRIDGE"
  echo "LEAN_CERT_WF=$CERT_WF"
  echo "LEAN_MAX_WIDTH=$MAX_WIDTH"
  printf 'FILE=%s\n' "${FILES[@]}"
} > "$LOG_DIR/preflight.log"

RESULT_JSON="$LOG_DIR/lhd_compile_result.json"
RUN_LOG="$LOG_DIR/lhd_compile.log"

set +e
"$LHD" compile verilog \
  "${FILES[@]}" \
  --reader yosys-slang \
  --top "$TOP" \
  --workdir "$WORK_DIR" \
  --result-json "$RESULT_JSON" \
  --emit-dir lg:"$LG_DIR" \
  --emit-dir lean:"$LEAN_DIR" \
  --set yosys.setundef=zero \
  --set formal.lean.strict=true \
  --set formal.lean.normalize=true \
  --set formal.lean.emit_cert="$EMIT_CERT" \
  --set formal.lean.emit_fast_bridge="$EMIT_FAST_BRIDGE" \
  --set formal.lean.cert_wf="$CERT_WF" \
  --set formal.lean.max_width="$MAX_WIDTH" \
  > "$RUN_LOG" 2>&1
status=$?
set -e

echo "lhd exit status: $status"
tail -60 "$RUN_LOG" || true
[[ "$status" -eq 0 ]] || exit "$status"

generated="$LEAN_DIR/${TOP}_Lgraph.lean"
[[ -r "$generated" ]] || { echo "FATAL: missing generated Lean file: $generated" >&2; exit 2; }

# ---------------------------------------------------------------------------
# Static gates (seconds) -- never start a typecheck without them.
# ---------------------------------------------------------------------------
gate_status=0
{
  echo "== op census =="
  python3 "$LIVEHD_ROOT/pass/lean/scripts/op_census.py" "$generated" || gate_status=1
  if [[ "$EMIT_FAST_BRIDGE" == "true" ]]; then
    echo "== const parity =="
    python3 "$LIVEHD_ROOT/pass/lean/scripts/const_parity.py" "$generated" || gate_status=1
    n_sorry="$(grep -c '\bsorry\b' "$generated" || true)"
    n_todo="$(grep -c 'TODO(step5)' "$generated" || true)"
    echo "== sorry=$n_sorry TODO(step5)=$n_todo =="
    [[ "$n_sorry" == "0" && "$n_todo" == "0" ]] || gate_status=1
    echo "== _refines_fast theorems =="
    grep -oE '^theorem [A-Za-z0-9_]+_(comb|next|step)_refines_fast' "$generated" || true
    grep -q "_comb_refines_fast" "$generated" || gate_status=1
  fi
  echo "gate_status=$gate_status"
} > "$LOG_DIR/static_gates.log" 2>&1
echo "Static gates: $LOG_DIR/static_gates.log (gate_status=$gate_status)"
tail -40 "$LOG_DIR/static_gates.log" || true
[[ "$gate_status" -eq 0 ]] || { echo "FATAL: static gates failed" >&2; exit 3; }

if [[ "$RUN_LEAN" == "true" ]]; then
  set +e
  (
    cd "$LIVEHD_ROOT/formal/lean"
    LEAN_NUM_THREADS="$LEAN_JOBS" taskset -c "$LEAN_CPUSET" nice -n 19 ionice -c 3 \
      /usr/bin/time -f 'lean wall=%e s peak_rss=%M KB' \
      "$LAKE" env lean "$generated"
  ) > "$LOG_DIR/lean_typecheck.log" 2>&1
  lean_status=$?
  set -e
  echo "lean exit=$lean_status" >> "$LOG_DIR/lean_typecheck.log"
  echo "lean typecheck exit: $lean_status"
  grep -E 'error:|wall=' "$LOG_DIR/lean_typecheck.log" | head -40 || true
  [[ "$lean_status" -eq 0 ]] || exit "$lean_status"
fi

echo "Generated: $generated"
echo "Logs: $LOG_DIR"
