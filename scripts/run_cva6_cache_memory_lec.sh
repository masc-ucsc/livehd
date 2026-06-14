#!/usr/bin/env bash
set -euo pipefail

# CVA6 cache/MMU memory-path RTL -> LGraph -> regenerated-Verilog LEC runner.
#
# This uses the same frontend path as pass.isabelle generation, captures the
# Yosys-elaborated pp.v as the reference, regenerates Verilog from the produced
# LGraph, and runs lhd check. It intentionally keeps all temporary/runtime
# files under generated/; do not use /tmp.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIVEHD_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

LHD="${LHD:-$LIVEHD_ROOT/bazel-bin/lhd/lhd}"
CVA6_FILELIST="${CVA6_FILELIST:-$LIVEHD_ROOT/generated/cva6_filelists/cv64a6_imafdc_sv39_hpdcache_wb.top_cva6.flistplus.f}"
OUT_ROOT="${OUT_ROOT:-$LIVEHD_ROOT/generated/cva6_cache_memory_lec}"
YOSYS_MEMORY_MODE="${YOSYS_MEMORY_MODE:-collect}"
ISABELLE_CERT_WF="${ISABELLE_CERT_WF:-skip}"

if [[ ! -x "$LHD" ]]; then
  echo "Missing lhd binary: $LHD" >&2
  echo "Build it first: cd $LIVEHD_ROOT && bazel build //lhd:lhd" >&2
  exit 2
fi

if [[ ! -r "$CVA6_FILELIST" ]]; then
  echo "Missing CVA6_FILELIST=$CVA6_FILELIST" >&2
  exit 2
fi

mkdir -p "$OUT_ROOT"

declare -a MODULES
if [[ $# -gt 0 ]]; then
  MODULES=("$@")
else
  MODULES=(
    tc_sram_gate
    hpdcache_regbank_wbyteenable_1rw_gate
    hpdcache_regbank_wmask_1rw_gate
    hpdcache_fifo_reg_gate
    cva6_tlb_gate
  )
fi

wrapper_for() {
  case "$1" in
    tc_sram_gate)
      printf '%s\n' "$LIVEHD_ROOT/scripts/cva6_module_wrappers/tc_sram_gate.sv"
      ;;
    hpdcache_regbank_wbyteenable_1rw_gate)
      printf '%s\n' "$LIVEHD_ROOT/scripts/cva6_module_wrappers/hpdcache_regbank_wbyteenable_1rw_gate.sv"
      ;;
    hpdcache_regbank_wmask_1rw_gate)
      printf '%s\n' "$LIVEHD_ROOT/scripts/cva6_module_wrappers/hpdcache_regbank_wmask_1rw_gate.sv"
      ;;
    hpdcache_fifo_reg_gate)
      printf '%s\n' "$LIVEHD_ROOT/scripts/cva6_module_wrappers/hpdcache_fifo_reg_gate.sv"
      ;;
    cva6_tlb_gate)
      printf '%s\n' "$LIVEHD_ROOT/scripts/cva6_module_wrappers/cva6_tlb_gate.sv"
      ;;
    *)
      echo "Unknown module '$1'. Pass a known wrapper top or extend wrapper_for()." >&2
      return 2
      ;;
  esac
}

write_root() {
  local top="$1"
  local isa_dir="$2"
  cat > "$isa_dir/ROOT" <<EOF
session "CVA6-${top}-Lgraph" = DINO_Semantic_Primitives_Test +
  options [document = false, browser_info = false]
  theories
    ${top}_Lgraph_Cert
EOF
}

run_one() {
  local top="$1"
  local wrapper
  wrapper="$(wrapper_for "$top")"
  if [[ ! -r "$wrapper" ]]; then
    echo "Missing wrapper for $top: $wrapper" >&2
    return 2
  fi

  local out="$OUT_ROOT/$top"
  local gen_dir="$out/generated_inputs"
  local logs="$out/logs"
  local run_cwd="$out/run_cwd"
  local work="$out/lhd_work"
  local lg="$out/lgdb"
  local isa="$out/isabelle"
  local synth_work="$out/synth_work"
  local lec_work="$out/lec_work"
  local filelist="$gen_dir/${top}.f"
  local anchor="$gen_dir/livehd_filelist_anchor.sv"
  local ref_v="$out/reference_pp.v"
  local gate_v="$out/regenerated_from_lgraph.v"

  mkdir -p "$gen_dir" "$logs" "$run_cwd" "$work" "$lg" "$isa" "$synth_work" "$lec_work"

  cp "$CVA6_FILELIST" "$filelist"
  printf '%s\n' "$wrapper" >> "$filelist"
  cat > "$anchor" <<'EOF'
// Empty anchor file. The real source list is supplied through yosys.filelist_file.
EOF

  cat > "$out/README.md" <<EOF
# CVA6 cache/memory LEC: $top

This directory captures:

- \`reference_pp.v\`: Yosys/slang elaborated reference emitted before yosys2lg.
- \`lgdb/\`: LGraph imported from the same Yosys design.
- \`isabelle/\`: pass.isabelle output.
- \`regenerated_from_lgraph.v\`: cgen Verilog emitted from the LGraph.
- \`lec_work/\`: lhd check logs comparing regenerated Verilog against \`reference_pp.v\`.

Yosys memory mode: \`$YOSYS_MEMORY_MODE\`
Certificate mode: \`$ISABELLE_CERT_WF\`
EOF

  echo "== $top: compile RTL/filelist to LGraph + Isabelle =="
  set +e
  (
    cd "$run_cwd"
    "$LHD" compile verilog \
      "$anchor" \
      --reader yosys-slang \
      --top "$top" \
      --workdir "$work" \
      --result-json "$logs/lhd_compile_result.json" \
      --emit-dir lg:"$lg" \
      --emit-dir isabelle:"$isa" \
      --set yosys.filelist_file="$filelist" \
      --set yosys.setundef=zero \
      --set yosys.memory_mode="$YOSYS_MEMORY_MODE" \
      --set isabelle.strict=true \
      --set isabelle.normalize=true \
      --set isabelle.max_width=1048576 \
      --set isabelle.cert_wf="$ISABELLE_CERT_WF" \
      -- \
      --ignore-assertions
  ) > "$logs/lhd_compile.log" 2>&1
  local compile_status=$?
  set -e
  if [[ $compile_status -ne 0 ]]; then
    echo "FAIL $top: lhd compile failed; see $logs/lhd_compile.log"
    tail -80 "$logs/lhd_compile.log" || true
    return "$compile_status"
  fi

  if [[ ! -r "$run_cwd/pp.v" ]]; then
    echo "FAIL $top: expected $run_cwd/pp.v from Yosys frontend" >&2
    return 1
  fi
  cp "$run_cwd/pp.v" "$ref_v"
  if [[ -r "$run_cwd/pp-mem.il" ]]; then
    cp "$run_cwd/pp-mem.il" "$out/reference_pp-mem.il"
  fi
  write_root "$top" "$isa"

  echo "== $top: synth LGraph back to Verilog =="
  "$LHD" synth lg:"$lg" \
    --top "$top" \
    --recipe O0 \
    --workdir "$synth_work" \
    --result-json "$logs/lhd_synth_result.json" \
    --emit verilog:"$gate_v" \
    > "$logs/lhd_synth.log" 2>&1

  echo "== $top: LEC regenerated Verilog vs Yosys reference =="
  set +e
  "$LHD" check \
    --impl verilog:"$gate_v" \
    --ref verilog:"$ref_v" \
    --impl-top "$top" \
    --ref-top "$top" \
    --workdir "$lec_work" \
    --result-json "$logs/lhd_check_result.json" \
    > "$logs/lhd_check.log" 2>&1
  local lec_status=$?
  set -e
  if [[ $lec_status -ne 0 ]]; then
    echo "FAIL $top: LEC failed; see $logs/lhd_check.log and $lec_work"
    tail -80 "$logs/lhd_check.log" || true
    return "$lec_status"
  fi

  echo "PASS $top"
}

overall=0
for top in "${MODULES[@]}"; do
  if ! run_one "$top"; then
    overall=1
    break
  fi
done

exit "$overall"
