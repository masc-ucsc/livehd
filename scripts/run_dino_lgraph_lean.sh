#!/usr/bin/env bash
set -euo pipefail

# Generate Lean LGraph models for the three DINO CPU implementations.
#
# Validation pipeline (plan Step 5) — order matters:
#   1. LiveHD compile   RTL -> LGraph
#   2. LEC gate         prove/classify RTL == LGraph   (run_dino_lgraph_lec_gate.sh)
#                       REFUTED aborts; INCONCLUSIVE warns (LEC_STRICT=true = hard)
#   3. pass.lean        LGraph -> Lean model + certificate   (this script)
#   4. Lean typecheck   lake env lean <Top>_Lgraph.lean      (RUN_LEAN=true)
#   5. cert bridge      generated model = graph certificate  (per-design theorems)
#
# The LEC gate (step 2) runs first unless RUN_LEC_GATE=false.
#
# This is the Lean analogue of the DINO pass.isabelle runner.  It keeps all
# runtime files under generated/ so shared machines do not depend on /tmp.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIVEHD_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

LHD="${LHD:-$LIVEHD_ROOT/bazel-bin/lhd/lhd}"
LAKE="${LAKE:-lake}"
RUN_LEAN="${RUN_LEAN:-true}"
RUN_LEC_GATE="${RUN_LEC_GATE:-true}"
STRICT="${LEAN_STRICT:-true}"
MAX_WIDTH="${LEAN_MAX_WIDTH:-1048576}"
EMIT_CERT="${LEAN_EMIT_CERT:-true}"

HAGENT_BUILD="${HAGENT_BUILD:-/mada/users/czeng14/projects/hagent/.cache/setup_simplechisel_mcp_2025.11/build}"
SC_DIR="${SC_DIR:-$HAGENT_BUILD/build_singlecyclecpu_d}"
PIPE_DIR="${PIPE_DIR:-$HAGENT_BUILD/build_pipelined_d}"
DUAL_DIR="${DUAL_DIR:-$HAGENT_BUILD/build_dualissue_d}"

OUT="${OUT:-$LIVEHD_ROOT/generated/dino_lgraph_lean}"
LOG_DIR="$OUT/logs"
WORK_ROOT="$OUT/lhd_work"
LEAN_DIR="$OUT/lean"
LG_DIR="$OUT/lgdb"
RUNTIME_TMP_DIR="$OUT/runtime_tmp"

mkdir -p "$LOG_DIR" "$WORK_ROOT" "$LEAN_DIR" "$LG_DIR" "$RUNTIME_TMP_DIR"

export TMPDIR="$RUNTIME_TMP_DIR"
export TMP="$RUNTIME_TMP_DIR"
export TEMP="$RUNTIME_TMP_DIR"

if [[ ! -x "$LHD" ]]; then
  echo "FATAL: missing lhd binary: $LHD" >&2
  echo "Run: cd $LIVEHD_ROOT && bazel build //lhd:lhd" >&2
  exit 2
fi

collect_sv_files() {
  local dir="$1"
  if [[ ! -d "$dir" ]]; then
    echo "FATAL: missing RTL directory: $dir" >&2
    exit 2
  fi
  find "$dir" -maxdepth 1 -type f -name '*.sv' | sort
}

run_design() {
  local top="$1"
  local dir="$2"
  local work="$WORK_ROOT/$top"
  local lg="$LG_DIR/$top"
  local log="$LOG_DIR/${top}.log"
  local result="$LOG_DIR/${top}_result.json"
  mapfile -t files < <(collect_sv_files "$dir")
  if [[ "${#files[@]}" -eq 0 ]]; then
    echo "FATAL: no .sv files found in $dir" >&2
    exit 2
  fi

  mkdir -p "$work" "$lg"
  echo "[pass.lean] $top"
  set +e
  "$LHD" compile verilog \
    "${files[@]}" \
    --reader yosys-verilog \
    --top "$top" \
    --workdir "$work" \
    --result-json "$result" \
    --emit-dir lg:"$lg" \
    --emit-dir lean:"$LEAN_DIR" \
    --set yosys.setundef=zero \
    --set lean.strict="$STRICT" \
    --set lean.normalize=true \
    --set lean.emit_cert="$EMIT_CERT" \
    --set lean.max_width="$MAX_WIDTH" \
    > "$log" 2>&1
  local status=$?
  set -e
  echo "  status: $status"
  if [[ "$status" -ne 0 ]]; then
    tail -80 "$log" >&2 || true
    exit "$status"
  fi

  if [[ "$RUN_LEAN" == "true" ]]; then
    local generated="$LEAN_DIR/${top}_Lgraph.lean"
    if [[ ! -r "$generated" ]]; then
      echo "FATAL: expected generated Lean file missing: $generated" >&2
      exit 2
    fi
    (
      cd "$LIVEHD_ROOT/formal/lean"
      "$LAKE" env lean "$generated"
    ) > "$LOG_DIR/${top}_lean_typecheck.log" 2>&1
  fi
}

# Step 2 (pipeline order): LEC frontend gate — prove RTL == LGraph before any
# theorem-prover generation.  REFUTED aborts; INCONCLUSIVE is a recorded warning
# unless LEC_STRICT=true.  Skip with RUN_LEC_GATE=false (e.g. model-only bring-up).
if [[ "$RUN_LEC_GATE" == "true" ]]; then
  echo "[pipeline] step 2/5: LEC gate (RTL == LGraph) before pass.lean"
  if ! LHD="$LHD" HAGENT="$HAGENT_BUILD" OUT="$OUT/lec_gate" LEC_STRICT="${LEC_STRICT:-false}" \
       bash "$SCRIPT_DIR/run_dino_lgraph_lec_gate.sh"; then
    echo "FATAL: LEC gate reported REFUTED (or strict INCONCLUSIVE); not generating Lean" >&2
    exit 3
  fi
else
  echo "[pipeline] step 2/5: LEC gate SKIPPED (RUN_LEC_GATE=false)"
fi

echo "[pipeline] step 3/5: pass.lean generation"
run_design SingleCycleCPU "$SC_DIR"
run_design PipelinedCPU "$PIPE_DIR"
run_design PipelinedDualIssueCPU "$DUAL_DIR"

if grep -RIn "sorry\\|admit\\|undefined\\|Memory node" "$LEAN_DIR" "$LOG_DIR" >/dev/null 2>&1; then
  echo "WARNING: generated Lean/logs contain proof-trust or unsupported markers" >&2
  grep -RIn "sorry\\|admit\\|undefined\\|Memory node" "$LEAN_DIR" "$LOG_DIR" | head -80 >&2 || true
fi

echo "Generated DINO Lean files: $LEAN_DIR"
echo "Logs: $LOG_DIR"
