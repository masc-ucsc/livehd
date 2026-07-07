#!/usr/bin/env bash
set -euo pipefail

# Full-core CVA6 pass.lean stress runner.
#
# This is a completeness probe. It is expected to expose missing pass.lean
# semantics before it becomes a passing gate. Keep FPnew blackboxed initially.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIVEHD_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CVA6_ROOT="${CVA6_ROOT:-/mada/users/czeng14/projects/cva6-clean/cva6}"
TARGET="${CVA6_TARGET:-cv64a6_imafdc_sv39_hpdcache_wb}"
TOP="${CVA6_TOP:-cva6}"
LHD="${LHD:-$LIVEHD_ROOT/bazel-bin/lhd/lhd}"
EMIT_CERT="${LEAN_EMIT_CERT:-true}"
BENDER="${BENDER:-}"
if [[ -z "$BENDER" ]]; then
  if command -v bender >/dev/null 2>&1; then
    BENDER="$(command -v bender)"
  elif [[ -x /mada/users/czeng14/.local/bin/bender ]]; then
    BENDER=/mada/users/czeng14/.local/bin/bender
  fi
fi

case "$TARGET" in
  cv64a6_imafdcv_sv39) CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdcv_sv39_config_pkg.sv" ;;
  cv64a6_imafdch_sv39) CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdch_sv39_config_pkg.sv" ;;
  cv64a6_imafdc_sv39_hpdcache_wb) CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdc_sv39_hpdcache_wb_config_pkg.sv" ;;
  cv32a6_imac_sv32) CONFIG_FILE="$CVA6_ROOT/core/include/cv32a6_imac_sv32_config_pkg.sv" ;;
  *)
    echo "Unsupported CVA6_TARGET=$TARGET" >&2
    exit 2
    ;;
esac

OUT="${OUT:-$LIVEHD_ROOT/generated/cva6_complex_lean_${TARGET}}"
LOG_DIR="$OUT/logs"
WORK_DIR="$OUT/lhd_work"
LG_DIR="$OUT/lgdb"
LEAN_DIR="$OUT/lean"
GEN_DIR="$OUT/generated_inputs"
RUNTIME_TMP_DIR="$OUT/runtime_tmp"
FILELIST="$GEN_DIR/${TARGET}.f"
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
    "$BENDER" script flist-plus -t "$TARGET" --top "$TOP" --trim-incdirs auto
  ) > "$FILELIST"
else
  echo "FATAL: bender not found and CVA6_FILELIST not supplied" >&2
  exit 2
fi

cat > "$DUMMY_SV" <<'EOF'
// Empty anchor file. Real CVA6 sources are supplied through yosys.filelist_file.
EOF

declare -a SLANG_FLAGS=("--ignore-assertions")
if [[ -n "${CVA6_BLACKBOX_MODULES:-fpnew_top fpnew_top_multi}" ]]; then
  # shellcheck disable=SC2206
  blackboxes=(${CVA6_BLACKBOX_MODULES:-fpnew_top fpnew_top_multi})
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
  echo "CONFIG_FILE=$CONFIG_FILE"
  echo "FILELIST=$FILELIST"
  echo "OUT=$OUT"
  echo "LEAN_EMIT_CERT=$EMIT_CERT"
  echo "SLANG_FLAGS=${SLANG_FLAGS[*]}"
} > "$LOG_DIR/preflight.log"

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
  --set lean.strict=true \
  --set lean.normalize=true \
  --set lean.emit_cert="$EMIT_CERT" \
  --set lean.max_width=1048576 \
  -- \
  "${SLANG_FLAGS[@]}" \
  > "$RUN_LOG" 2>&1
status=$?
set -e

echo "lhd exit status: $status"
tail -80 "$RUN_LOG" || true
exit "$status"
