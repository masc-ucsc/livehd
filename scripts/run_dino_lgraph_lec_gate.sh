#!/usr/bin/env bash
set -euo pipefail
#
# DINO LEC frontend gate (plan §Step 4).
#
# Genuine RTL-vs-graph equivalence gate:
#   impl = the post-cprop LGraph compiled from the RTL
#   ref  = the raw RTL (all .sv concatenated), freshly elaborated
# cprop transforms the impl side, so formal.lec.semdiff cannot trivially short-circuit;
# the auto portfolio (ind|bmc, cvc5) proves semantic equivalence via flop-cut
# miters.  This catches translation bugs a dual-compile determinism check would
# miss (e.g. an identical mistranslation on both compiles).
#
# See also: scripts/run_cva6_cache_memory_lec.sh (synth->verilog regeneration LEC)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIVEHD_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

LHD="${LHD:-$LIVEHD_ROOT/bazel-bin/lhd/lhd}"
HAGENT="${HAGENT:-/mada/users/czeng14/projects/hagent/.cache/setup_simplechisel_mcp_2025.11/build}"
OUT="${OUT:-$LIVEHD_ROOT/generated/dino_lgraph_lec_gate}"
LEC_STRICT="${LEC_STRICT:-false}"

declare -a DESIGNS
DESIGNS=(
  "SingleCycleCPU:${HAGENT}/build_singlecyclecpu_d:SingleCycleCPU"
  "PipelinedCPU:${HAGENT}/build_pipelined_d:PipelinedCPU"
  "PipelinedDualIssueCPU:${HAGENT}/build_dualissue_d:PipelinedDualIssueCPU"
)

lec_status_global=0
proven=0; refuted=0; inconclusive=0; total=0

for entry in "${DESIGNS[@]}"; do
  IFS=":" read -r name dir top <<< "$entry"
  work="$OUT/$name/work"
  lg="$OUT/$name/lg"
  ref_sv="$OUT/$name/raw_${top}.sv"
  lec_work="$OUT/$name/lec"
  log="$OUT/$name.log"

  if [[ ! -d "$dir" ]]; then
    echo "FATAL: missing RTL directory for $name: $dir" >&2; exit 2
  fi

  echo -n "[LEC gate] $name: "
  mkdir -p "$work" "$lg" "$lec_work"

  mapfile -t sv_files < <(find "$dir" -maxdepth 1 -name '*.sv' | sort)

  # impl side: compile the RTL to a post-cprop LGraph
  "$LHD" compile verilog "${sv_files[@]}" --reader yosys-verilog --top "$top" \
    --workdir "$work" --emit-dir lg:"$lg" \
    --set yosys.setundef=zero > "$log" 2>&1

  # ref side: raw RTL, all modules concatenated into one .sv (independent elab)
  cat "${sv_files[@]}" > "$ref_sv"

  # RTL-vs-graph LEC: post-cprop lg (impl) vs raw RTL (ref).  Solver-backed
  # (auto = ind|bmc portfolio, cvc5); semdiff cannot trivially match because
  # cprop reshaped the impl side.
  set +e
  "$LHD" lec --impl lg:"$lg" --ref verilog:"$ref_sv" --top "$top" \
    --reader yosys-verilog --workdir "$lec_work" \
    --set formal.engine=auto --set formal.lec.hier=true \
    --set formal.lec.semdiff=structural --set formal.strict="$LEC_STRICT" \
    --result-json "$OUT/${name}_lec.json" >> "$log" 2>&1
  rc=$?; set -e

  # distinguish PROVEN vs REFUTED vs inconclusive from the verdict json/log
  verdict="$(python3 -c "import json,sys;d=json.load(open('$OUT/${name}_lec.json'));print(d.get('status','?'))" 2>/dev/null || echo '?')"
  by_solver="$(grep -c 'via solver' "$log" 2>/dev/null || echo 0)"
  if [[ $rc -eq 0 && "$verdict" == "pass" ]]; then
    echo "PROVEN ✓  (solver-backed: $(grep -oE '[0-9]+ via solver' "$log" | tail -1))"; ((proven++)) || :
  elif grep -qiE 'REFUTED|not equivalent|counterexample' "$log" 2>/dev/null; then
    echo "REFUTED ✗"; ((refuted++)) || :; lec_status_global=1
  else
    echo "INCONCLUSIVE ⚠ (rc=$rc, verdict=$verdict)"; ((inconclusive++)) || :
    [[ "$LEC_STRICT" == "true" ]] && lec_status_global=1
  fi
  ((total++)) || :
done

echo ""
echo "LEC gate: $total designs — proven=$proven refuted=$refuted inconclusive=$inconclusive strict=$LEC_STRICT"
exit "$lec_status_global"
