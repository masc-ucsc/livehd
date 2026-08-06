#!/usr/bin/env bash
set -euo pipefail

# Module-level CVA6 pass.lean stress runner.
#
# Use this before full-core CVA6. It preserves the same CVA6 filelist/config
# handling as the pass.isabelle runner, but emits Lean artifacts.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIVEHD_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CVA6_ROOT="${CVA6_ROOT:-/mada/users/czeng14/projects/cva6-clean/cva6}"
TARGET="${CVA6_TARGET:-cv64a6_imafdc_sv39_hpdcache_wb}"
TOP="${CVA6_TOP:-tc_sram}"
BENDER_TOP="${CVA6_BENDER_TOP:-$TOP}"
LHD="${LHD:-$LIVEHD_ROOT/bazel-bin/lhd/lhd}"
LAKE="${LAKE:-lake}"
RUN_LEAN="${RUN_LEAN:-false}"
EMIT_CERT="${LEAN_EMIT_CERT:-true}"
# Step-5 fast-view bridge (<Top>_comb/_next/_step = _cert).  Default off, matching
# the pass default, because a bridge-enabled file is much more expensive to
# typecheck.  Requires EMIT_CERT=true and a memory-free module.
EMIT_FAST_BRIDGE="${LEAN_EMIT_FAST_BRIDGE:-false}"
CERT_WF="${LEAN_CERT_WF:-skip}"
# Good-citizen caps for the Lean typecheck (this box is an NFS server).
LEAN_JOBS="${LEAN_JOBS:-8}"
LEAN_CPUSET="${LEAN_CPUSET:-0-7}"
BENDER="${BENDER:-}"
if [[ -z "$BENDER" ]]; then
  if command -v bender >/dev/null 2>&1; then
    BENDER="$(command -v bender)"
  elif [[ -x /mada/users/czeng14/.local/bin/bender ]]; then
    BENDER=/mada/users/czeng14/.local/bin/bender
  fi
fi

sanitize() {
  printf '%s' "$1" | tr -c 'A-Za-z0-9_-' '_'
}

case "$TARGET" in
  cv64a6_imafdcv_sv39) CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdcv_sv39_config_pkg.sv" ;;
  cv64a6_imafdch_sv39) CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdch_sv39_config_pkg.sv" ;;
  cv64a6_imafdch_sv39_wb) CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdch_sv39_wb_config_pkg.sv" ;;
  cv64a6_imafdc_sv39) CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdc_sv39_config_pkg.sv" ;;
  cv64a6_imafdc_sv39_wb) CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdc_sv39_wb_config_pkg.sv" ;;
  cv64a6_imafdc_sv39_hpdcache) CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdc_sv39_hpdcache_config_pkg.sv" ;;
  cv64a6_imafdc_sv39_hpdcache_wb) CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdc_sv39_hpdcache_wb_config_pkg.sv" ;;
  cv32a6_imac_sv32) CONFIG_FILE="$CVA6_ROOT/core/include/cv32a6_imac_sv32_config_pkg.sv" ;;
  *)
    echo "Unsupported CVA6_TARGET=$TARGET" >&2
    exit 2
    ;;
esac

TARGET_SAFE="$(sanitize "$TARGET")"
TOP_SAFE="$(sanitize "$TOP")"
OUT="${OUT:-$LIVEHD_ROOT/generated/cva6_module_lean_${TARGET_SAFE}_${TOP_SAFE}}"
LOG_DIR="$OUT/logs"
WORK_DIR="$OUT/lhd_work"
LG_DIR="$OUT/lgdb"
LEAN_DIR="$OUT/lean"
GEN_DIR="$OUT/generated_inputs"
RUNTIME_TMP_DIR="$OUT/runtime_tmp"
FILELIST="$GEN_DIR/${TARGET_SAFE}_${TOP_SAFE}.f"
DUMMY_SV="$GEN_DIR/livehd_filelist_anchor.sv"
RESULT_JSON="$LOG_DIR/lhd_compile_result.json"
RUN_LOG="$LOG_DIR/lhd_compile.log"

mkdir -p "$LOG_DIR" "$WORK_DIR" "$LG_DIR" "$LEAN_DIR" "$GEN_DIR" "$RUNTIME_TMP_DIR"
export TMPDIR="$RUNTIME_TMP_DIR"
export TMP="$RUNTIME_TMP_DIR"
export TEMP="$RUNTIME_TMP_DIR"

if [[ ! -x "$LHD" ]]; then
  echo "FATAL: missing lhd binary: $LHD" >&2
  echo "Run: cd $LIVEHD_ROOT && bazel build //lhd:lhd" >&2
  exit 2
fi
if [[ ! -r "$CONFIG_FILE" ]]; then
  echo "FATAL: missing selected config file: $CONFIG_FILE" >&2
  exit 2
fi

if [[ -n "${CVA6_FILELIST:-}" ]]; then
  cp "$CVA6_FILELIST" "$FILELIST"
elif [[ -n "$BENDER" && -x "$BENDER" ]]; then
  (
    cd "$CVA6_ROOT"
    "$BENDER" script flist-plus -t "$TARGET" --top "$BENDER_TOP" --trim-incdirs auto
  ) > "$FILELIST"
else
  echo "FATAL: bender not found and CVA6_FILELIST not supplied" >&2
  exit 2
fi

cat > "$DUMMY_SV" <<'EOF'
// Empty anchor file. Real CVA6 sources are supplied through yosys.filelist_file.
EOF

if [[ -n "${CVA6_WRAPPER_FILE:-}" ]]; then
  [[ -r "$CVA6_WRAPPER_FILE" ]] || { echo "FATAL: missing CVA6_WRAPPER_FILE=$CVA6_WRAPPER_FILE" >&2; exit 2; }
  printf '%s\n' "$CVA6_WRAPPER_FILE" >> "$FILELIST"
fi

declare -a SLANG_FLAGS=("--ignore-assertions")
if [[ -n "${CVA6_BLACKBOX_MODULES:-}" ]]; then
  # shellcheck disable=SC2206
  blackboxes=(${CVA6_BLACKBOX_MODULES})
  for module in "${blackboxes[@]}"; do
    SLANG_FLAGS+=("--blackboxed-module" "$module")
  done
fi
if [[ -n "${CVA6_EXTRA_SLANG_FLAGS:-}" ]]; then
  # shellcheck disable=SC2206
  SLANG_FLAGS+=(${CVA6_EXTRA_SLANG_FLAGS})
fi

{
  echo "CVA6_ROOT=$CVA6_ROOT"
  echo "TARGET=$TARGET"
  echo "TOP=$TOP"
  echo "BENDER_TOP=$BENDER_TOP"
  echo "CONFIG_FILE=$CONFIG_FILE"
  echo "FILELIST=$FILELIST"
  echo "OUT=$OUT"
  echo "RUN_LEAN=$RUN_LEAN"
  echo "LEAN_EMIT_CERT=$EMIT_CERT"
  echo "LEAN_EMIT_FAST_BRIDGE=$EMIT_FAST_BRIDGE"
  echo "LEAN_CERT_WF=$CERT_WF"
} > "$LOG_DIR/preflight.log"

if [[ "$EMIT_FAST_BRIDGE" == "true" && "$EMIT_CERT" != "true" ]]; then
  echo "FATAL: LEAN_EMIT_FAST_BRIDGE=true requires LEAN_EMIT_CERT=true" >&2
  exit 2
fi

set +e
"$LHD" compile verilog \
  "$DUMMY_SV" \
  --reader yosys-slang \
  --top "$TOP" \
  --workdir "$WORK_DIR" \
  --result-json "$RESULT_JSON" \
  --emit-dir lg:"$LG_DIR" \
  --emit-dir lean:"$LEAN_DIR" \
  --set yosys.filelist_file="$FILELIST" \
  --set yosys.setundef=zero \
  ${YOSYS_MEMORY_MODE:+--set yosys.memory_mode="$YOSYS_MEMORY_MODE"} \
  --set formal.lean.strict=true \
  --set formal.lean.normalize=true \
  --set formal.lean.emit_cert="$EMIT_CERT" \
  --set formal.lean.emit_fast_bridge="$EMIT_FAST_BRIDGE" \
  --set formal.lean.cert_wf="$CERT_WF" \
  --set formal.lean.max_width=1048576 \
  -- \
  "${SLANG_FLAGS[@]}" \
  > "$RUN_LOG" 2>&1
status=$?
set -e

echo "lhd exit status: $status"
tail -80 "$RUN_LOG" || true
if [[ "$status" -ne 0 ]]; then
  exit "$status"
fi

generated="$LEAN_DIR/${TOP}_Lgraph.lean"

# ---------------------------------------------------------------------------
# Static gates (seconds).  These run BEFORE any typecheck: they cost nothing and
# replace hours of discovery.  See pass/lean/README.md "Static gates first".
# ---------------------------------------------------------------------------
gate_status=0
if [[ -r "$generated" ]]; then
  {
    echo "== op census =="
    python3 "$LIVEHD_ROOT/pass/lean/scripts/op_census.py" "$generated" || gate_status=1
    if [[ "$EMIT_FAST_BRIDGE" == "true" ]]; then
      echo "== const parity =="
      python3 "$LIVEHD_ROOT/pass/lean/scripts/const_parity.py" "$generated" || gate_status=1
      echo "== sorry / TODO(step5) =="
      n_sorry="$(grep -c '\bsorry\b' "$generated" || true)"
      n_todo="$(grep -c 'TODO(step5)' "$generated" || true)"
      echo "sorry=$n_sorry TODO(step5)=$n_todo"
      [[ "$n_sorry" == "0" && "$n_todo" == "0" ]] || gate_status=1
      echo "== _refines_fast theorems =="
      grep -oE '^theorem [A-Za-z0-9_]+_(comb|next|step)_refines_fast' "$generated" || true
      # _comb is always required; _next/_step only for a sequential design.
      grep -q "_comb_refines_fast" "$generated" || gate_status=1
    fi
    echo "gate_status=$gate_status"
  } > "$LOG_DIR/static_gates.log" 2>&1
  echo "Static gates: $LOG_DIR/static_gates.log (gate_status=$gate_status)"
  tail -40 "$LOG_DIR/static_gates.log" || true
fi
if [[ "$gate_status" -ne 0 ]]; then
  echo "FATAL: static gates failed; not starting a typecheck" >&2
  exit 3
fi

if [[ "$RUN_LEAN" == "true" ]]; then
  [[ -r "$generated" ]] || { echo "FATAL: missing generated Lean file: $generated" >&2; exit 2; }
  # Good-citizen caps: this box is an NFS server, an unbounded Lean starves it.
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

echo "Generated CVA6 module Lean files: $LEAN_DIR"
echo "Logs: $LOG_DIR"
