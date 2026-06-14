#!/usr/bin/env bash
set -euo pipefail

# Stress pass.isabelle with a high-feature CVA6 configuration.
#
# This is intentionally a completeness test, not a "make it pass by stubbing"
# script. It keeps pass.isabelle in strict mode and records the first missing
# semantic feature in project-local logs.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIVEHD_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

CVA6_ROOT="${CVA6_ROOT:-/soe/czeng14/projects/cva6-clean/cva6}"
TARGET="${CVA6_TARGET:-cv64a6_imafdc_sv39_hpdcache_wb}"
TOP="${CVA6_TOP:-cva6}"
LHD="${LHD:-$LIVEHD_ROOT/bazel-bin/lhd/lhd}"
BENDER="${BENDER:-}"
if [[ -z "$BENDER" ]]; then
  if command -v bender >/dev/null 2>&1; then
    BENDER="$(command -v bender)"
  elif [[ -x /mada/users/czeng14/.local/bin/bender ]]; then
    BENDER=/mada/users/czeng14/.local/bin/bender
  fi
fi

case "$TARGET" in
  cv64a6_imafdcv_sv39)
    CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdcv_sv39_config_pkg.sv"
    ;;
  cv64a6_imafdch_sv39)
    CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdch_sv39_config_pkg.sv"
    ;;
  cv64a6_imafdc_sv39_hpdcache_wb)
    CONFIG_FILE="$CVA6_ROOT/core/include/cv64a6_imafdc_sv39_hpdcache_wb_config_pkg.sv"
    ;;
  cv32a6_imac_sv32)
    CONFIG_FILE="$CVA6_ROOT/core/include/cv32a6_imac_sv32_config_pkg.sv"
    ;;
  *)
    echo "Unsupported CVA6_TARGET=$TARGET" >&2
    echo "Known stress targets: cv64a6_imafdcv_sv39, cv64a6_imafdch_sv39, cv64a6_imafdc_sv39_hpdcache_wb, cv32a6_imac_sv32" >&2
    exit 2
    ;;
esac

OUT="${OUT:-$LIVEHD_ROOT/generated/cva6_complex_isabelle_${TARGET}}"
LOG_DIR="$OUT/logs"
WORK_DIR="$OUT/lhd_work"
LG_DIR="$OUT/lgdb"
ISA_DIR="$OUT/isabelle"
GEN_DIR="$OUT/generated_inputs"
FILELIST="$GEN_DIR/${TARGET}.f"
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

PREFLIGHT="$LOG_DIR/preflight.log"
: > "$PREFLIGHT"
{
  echo "CVA6_ROOT=$CVA6_ROOT"
  echo "TARGET=$TARGET"
  echo "CONFIG_FILE=$CONFIG_FILE"
  echo
  echo "Dependency probes:"
  for probe in \
    "$CVA6_ROOT/vendor/pulp-platform/common_cells/include/common_cells/registers.svh" \
    "$CVA6_ROOT/core/cache_subsystem/hpdcache/rtl/include/hpdcache_typedef.svh" \
    "$CVA6_ROOT/core/cache_subsystem/hpdcache/rtl/src/hpdcache_pkg.sv" \
    "$CVA6_ROOT/core/cvfpu/src/fpnew_pkg.sv" \
    "$CVA6_ROOT/vendor/pulp-platform/cvfpu/src/fpnew_pkg.sv"; do
    if [[ -r "$probe" ]]; then
      echo "  present: $probe"
    else
      echo "  missing: $probe"
    fi
  done
} >> "$PREFLIGHT"

write_fallback_filelist() {
  local tmp="$FILELIST.tmp"
  : > "$tmp"

  # Keep package/config order explicit. The selected CVA6 config package must
  # be the only file defining package cva6_config_pkg.
  cat >> "$tmp" <<EOF
$CVA6_ROOT/core/include/config_pkg.sv
$CONFIG_FILE
$CVA6_ROOT/core/include/riscv_pkg.sv
$CVA6_ROOT/core/include/ariane_pkg.sv
$CVA6_ROOT/core/include/build_config_pkg.sv
EOF

  for dir in "$CVA6_ROOT/core" "$CVA6_ROOT/common" "$CVA6_ROOT/vendor"; do
    [[ -d "$dir" ]] || continue
    find "$dir" -type f -name '*.sv' \
      ! -path "$CVA6_ROOT/core/include/config_pkg.sv" \
      ! -path "$CVA6_ROOT/core/include/riscv_pkg.sv" \
      ! -path "$CVA6_ROOT/core/include/ariane_pkg.sv" \
      ! -path "$CVA6_ROOT/core/include/build_config_pkg.sv" \
      ! -path "$CVA6_ROOT/core/include/*_config_pkg.sv" \
      ! -path "$CVA6_ROOT/core/include/*_config_pkg_*.sv" \
      ! -path "$CVA6_ROOT/core/include/deprecated_packages/*" \
      ! -path "$CVA6_ROOT/verif/*" \
      ! -path "$CVA6_ROOT/docs/*" \
      | sort >> "$tmp"
  done

  awk 'NF && !seen[$0]++ { print }' "$tmp" > "$FILELIST"
  rm -f "$tmp"
}

if [[ -n "${CVA6_FILELIST:-}" ]]; then
  cp "$CVA6_FILELIST" "$FILELIST"
elif [[ -n "$BENDER" && -x "$BENDER" ]]; then
  (
    cd "$CVA6_ROOT"
    "$BENDER" script flist-plus -t "$TARGET" --top "$TOP" --trim-incdirs auto
  ) > "$FILELIST"
else
  echo "bender not found; generating a conservative source filelist directly from $CVA6_ROOT" | tee "$LOG_DIR/filelist_note.log"
  write_fallback_filelist
fi

cat > "$DUMMY_SV" <<'EOF'
// Empty anchor file.
//
// lhd compile verilog currently requires at least one positional .sv input.
// The real CVA6 source list is supplied through yosys.filelist_file.
EOF

cat > "$OUT/README.md" <<EOF
# CVA6 pass.isabelle stress run

Target: \`$TARGET\`

Top: \`$TOP\`

Selected config package:

\`\`\`
$CONFIG_FILE
\`\`\`

Purpose:

- Exercise full CVA6 config expansion before \`pass.isabelle\`.
- Keep Yosys memories as memory nodes via the default \`memory -nomap\` flow.
- Normalize X/Z with \`setundef=zero\`.
- Keep \`pass.isabelle.strict=true\` so unsupported memory/SRAM/blackbox/large-interface semantics are reported instead of silently becoming \`undefined\`.

Main outputs:

- \`generated_inputs/${TARGET}.f\`
- \`lgdb/\`
- \`isabelle/\`
- \`logs/lhd_compile.log\`
- \`logs/lhd_compile_result.json\`
EOF

declare -a SLANG_FLAGS=("--ignore-assertions")

if [[ -n "${CVA6_FILELIST:-}" ]]; then
  # A Bender/filelist-driven run should own its include order. Adding broad
  # auto-discovered +incdir entries here can accidentally select a different
  # dependency checkout, e.g. old vendored common_cells headers instead of the
  # Bender-resolved common_cells version.
  if [[ -n "${CVA6_EXTRA_SLANG_FLAGS:-}" ]]; then
    # shellcheck disable=SC2206
    SLANG_FLAGS+=(${CVA6_EXTRA_SLANG_FLAGS})
  fi
else
  declare -a INCLUDE_DIRS=(
    "$CVA6_ROOT/core/include"
    "$CVA6_ROOT/core/cache_subsystem/hpdcache/rtl/include"
    "$CVA6_ROOT/core/cache_subsystem/hpdcache/rtl/src/utils/ecc"
    "$CVA6_ROOT/common/local/util"
    "$CVA6_ROOT/vendor/pulp-platform/axi/include"
    "$CVA6_ROOT/vendor/pulp-platform/common_cells/include"
  )

  while IFS= read -r svh; do
    dir="$(dirname "$svh")"
    parent="$(dirname "$dir")"
    INCLUDE_DIRS+=("$dir" "$parent")
  done < <(find "$CVA6_ROOT" -type f -name '*.svh' 2>/dev/null | sort)

  declare -A seen_inc=()
  for dir in "${INCLUDE_DIRS[@]}"; do
    [[ -d "$dir" ]] || continue
    [[ -z "${seen_inc[$dir]:-}" ]] || continue
    seen_inc[$dir]=1
    SLANG_FLAGS+=("+incdir+$dir")
  done
fi

printf '%s\n' "${SLANG_FLAGS[@]}" > "$GEN_DIR/slang_incdirs.args"

echo "Running CVA6 pass.isabelle stress test"
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
