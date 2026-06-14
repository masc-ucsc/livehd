#!/usr/bin/env bash
set -euo pipefail

# Module-level CVA6 pass.isabelle stress runner.
#
# This is the fast gate before attempting full CVA6. It uses the same
# Bender-resolved source universe as the full-core run, but asks slang/Yosys to
# elaborate a selected module top. Use this to bring cache/SRAM/MMU blocks to
# LGraph/pass.isabelle before debugging full-core FPnew scale.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIVEHD_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CVA6_ROOT="${CVA6_ROOT:-/mada/users/czeng14/projects/cva6-clean/cva6}"
TARGET="${CVA6_TARGET:-cv64a6_imafdc_sv39_hpdcache_wb}"
TOP="${CVA6_TOP:-tc_sram}"
BENDER_TOP="${CVA6_BENDER_TOP:-$TOP}"
LHD="${LHD:-$LIVEHD_ROOT/bazel-bin/lhd/lhd}"
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
  cv64a6_imafdcv_sv39)
    CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdcv_sv39_config_pkg.sv"
    ;;
  cv64a6_imafdch_sv39)
    CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdch_sv39_config_pkg.sv"
    ;;
  cv64a6_imafdch_sv39_wb)
    CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdch_sv39_wb_config_pkg.sv"
    ;;
  cv64a6_imafdc_sv39)
    CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdc_sv39_config_pkg.sv"
    ;;
  cv64a6_imafdc_sv39_wb)
    CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdc_sv39_wb_config_pkg.sv"
    ;;
  cv64a6_imafdc_sv39_hpdcache)
    CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdc_sv39_hpdcache_config_pkg.sv"
    ;;
  cv64a6_imafdc_sv39_hpdcache_wb)
    CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdc_sv39_hpdcache_wb_config_pkg.sv"
    ;;
  cv32a6_imac_sv32)
    CONFIG_FILE="$CVA6_ROOT/core/include/cv32a6_imac_sv32_config_pkg.sv"
    ;;
  *)
    echo "Unsupported CVA6_TARGET=$TARGET" >&2
    echo "Known targets include cv64a6_imafdc_sv39_hpdcache_wb and cv32a6_imac_sv32" >&2
    exit 2
    ;;
esac

TARGET_SAFE="$(sanitize "$TARGET")"
TOP_SAFE="$(sanitize "$TOP")"
OUT="${OUT:-$LIVEHD_ROOT/generated/cva6_module_isabelle_${TARGET_SAFE}_${TOP_SAFE}}"
LOG_DIR="$OUT/logs"
WORK_DIR="$OUT/lhd_work"
LG_DIR="$OUT/lgdb"
ISA_DIR="$OUT/isabelle"
GEN_DIR="$OUT/generated_inputs"
FILELIST="$GEN_DIR/${TARGET_SAFE}_${TOP_SAFE}.f"
DUMMY_SV="$GEN_DIR/livehd_filelist_anchor.sv"
RESULT_JSON="$LOG_DIR/lhd_compile_result.json"
RUN_LOG="$LOG_DIR/lhd_compile.log"

mkdir -p "$LOG_DIR" "$WORK_DIR" "$LG_DIR" "$ISA_DIR" "$GEN_DIR"

if [[ ! -x "$LHD" ]]; then
  echo "Missing lhd binary: $LHD" >&2
  echo "Build it first, e.g. cd $LIVEHD_ROOT && bazel build //lhd:lhd" >&2
  exit 2
fi

if [[ ! -r "$CONFIG_FILE" ]]; then
  echo "Missing selected config file: $CONFIG_FILE" >&2
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
  echo "bender not found and CVA6_FILELIST not supplied" >&2
  echo "Install bender or pass a Bender-generated flist-plus file via CVA6_FILELIST" >&2
  exit 2
fi

cat > "$DUMMY_SV" <<'EOF'
// Empty anchor file.
//
// lhd compile verilog currently requires at least one positional .sv input.
// The real CVA6 source list is supplied through yosys.filelist_file.
EOF

if [[ -n "${CVA6_WRAPPER_FILE:-}" ]]; then
  if [[ ! -r "$CVA6_WRAPPER_FILE" ]]; then
    echo "Missing CVA6_WRAPPER_FILE=$CVA6_WRAPPER_FILE" >&2
    exit 2
  fi
  BUILD_CONFIG_FILE="$CVA6_ROOT/core/include/build_config_pkg.sv"
  if grep -q "build_config_pkg::" "$CVA6_WRAPPER_FILE"; then
    if [[ -r "$BUILD_CONFIG_FILE" ]] && ! grep -Fxq "$BUILD_CONFIG_FILE" "$FILELIST"; then
      printf '%s\n' "$BUILD_CONFIG_FILE" >> "$FILELIST"
    fi
  fi
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

printf '%s\n' "${SLANG_FLAGS[@]}" > "$GEN_DIR/slang_flags.args"

cat > "$OUT/README.md" <<EOF
# CVA6 module pass.isabelle stress run

Target: \`$TARGET\`

Top module: \`$TOP\`

Underlying Bender trim top: \`$BENDER_TOP\`

Selected config package:

\`\`\`
$CONFIG_FILE
\`\`\`

Purpose:

- Elaborate one CVA6 module through yosys-slang/Yosys/LiveHD.
- Reach LGraph and \`pass.isabelle\` without full-core FPnew/cache/MMU scale.
- Keep strict Isabelle generation so unsupported semantics fail explicitly.

Main outputs:

- \`generated_inputs/${TARGET_SAFE}_${TOP_SAFE}.f\`
- \`generated_inputs/slang_flags.args\`
- \`lgdb/\`
- \`isabelle/\`
- \`logs/lhd_compile.log\`
- \`logs/lhd_compile_result.json\`
EOF

{
  echo "CVA6_ROOT=$CVA6_ROOT"
  echo "TARGET=$TARGET"
  echo "TOP=$TOP"
  echo "BENDER_TOP=$BENDER_TOP"
  echo "CVA6_WRAPPER_FILE=${CVA6_WRAPPER_FILE:-<none>}"
  echo "CONFIG_FILE=$CONFIG_FILE"
  echo "BENDER=${BENDER:-<none>}"
  echo "LHD=$LHD"
  echo "OUT=$OUT"
  echo
  echo "Slang flags:"
  printf '  %q\n' "${SLANG_FLAGS[@]}"
  echo
  echo "Filelist line count:"
  wc -l "$FILELIST"
  echo
  echo "First filelist lines:"
  sed -n '1,40p' "$FILELIST"
} > "$LOG_DIR/preflight.log"

echo "Running CVA6 module pass.isabelle stress test"
echo "  target:   $TARGET"
echo "  top:      $TOP"
echo "  out:      $OUT"
echo "  filelist: $FILELIST"
echo "  log:      $RUN_LOG"

set +e
"$LHD" compile verilog \
  "$DUMMY_SV" \
  --reader yosys-slang \
  --top "$TOP" \
  --workdir "$WORK_DIR" \
  --result-json "$RESULT_JSON" \
  --emit-dir lg:"$LG_DIR" \
  --emit-dir isabelle:"$ISA_DIR" \
  --set yosys.filelist_file="$FILELIST" \
  --set yosys.setundef=zero \
  ${YOSYS_MEMORY_MODE:+--set yosys.memory_mode="$YOSYS_MEMORY_MODE"} \
  --set isabelle.strict=true \
  --set isabelle.normalize=true \
  --set isabelle.max_width=1048576 \
  --set isabelle.cert_wf=skip \
  -- \
  "${SLANG_FLAGS[@]}" \
  > "$RUN_LOG" 2>&1
status=$?
set -e

echo "lhd exit status: $status"
if [[ -s "$RESULT_JSON" ]]; then
  echo "result json: $RESULT_JSON"
fi
echo "last log lines:"
tail -80 "$RUN_LOG" || true

exit "$status"
