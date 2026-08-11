#!/usr/bin/env bash
set -euo pipefail

# Generate Rocq LGraph models + graph certificates for the three DINO CPUs.
#
# Validation pipeline — order matters:
#   1. LiveHD compile   RTL -> LGraph
#   2. LEC gate         prove/classify RTL == LGraph   (run_dino_lgraph_lec_gate.sh)
#                       REFUTED aborts; INCONCLUSIVE warns (LEC_STRICT=true = hard)
#   3. pass.rocq        LGraph -> <Top>_Lgraph.v + <Top>_Lgraph_Cert.v
#   4. Rocq typecheck   rocq makefile + make                 (RUN_ROCQ=true)
#   5. cert bridge      generated model = graph certificate  (per-design theorems)
#
# The LEC gate (step 2) runs first unless RUN_LEC_GATE=false.  Nothing may be
# generated from a graph that LEC refuted: steps 3-5 only claim "generated model
# = LGraph certificate", and it is the LEC gate that ties the LGraph to the RTL.
#
# This is the Rocq analogue of run_dino_lgraph_lean.sh / run_dino_lgraph_isabelle.sh.
# All runtime files stay under generated/ so shared machines never depend on /tmp.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIVEHD_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

LHD="${LHD:-$LIVEHD_ROOT/bazel-bin/lhd/lhd}"
ROCQ="${ROCQ:-rocq}"
RUN_ROCQ="${RUN_ROCQ:-true}"
RUN_LEC_GATE="${RUN_LEC_GATE:-true}"
STRICT="${ROCQ_STRICT:-true}"
MAX_WIDTH="${ROCQ_MAX_WIDTH:-1048576}"
EMIT_CERT="${ROCQ_EMIT_CERT:-true}"
CERT_WF="${ROCQ_CERT_WF:-skip}"
CERT_WF_FALLBACK="${ROCQ_CERT_WF_FALLBACK:-fail}"
CERT_CHUNK_SIZE="${ROCQ_CERT_CHUNK_SIZE:-25}"
CERT_CHUNK_LIMIT="${ROCQ_CERT_CHUNK_LIMIT:-0}"
EVAL_ENGINE="${ROCQ_EVAL_ENGINE:-vm}"
JOBS="${JOBS:-4}"

HAGENT_BUILD="${HAGENT_BUILD:-/mada/users/czeng14/projects/hagent/.cache/setup_simplechisel_mcp_2025.11/build}"
SC_DIR="${SC_DIR:-$HAGENT_BUILD/build_singlecyclecpu_d}"
PIPE_DIR="${PIPE_DIR:-$HAGENT_BUILD/build_pipelined_d}"
DUAL_DIR="${DUAL_DIR:-$HAGENT_BUILD/build_dualissue_d}"

DESIGNS="${DESIGNS:-SingleCycleCPU PipelinedCPU PipelinedDualIssueCPU}"

OUT="${OUT:-$LIVEHD_ROOT/generated/dino_lgraph_rocq}"
LOG_DIR="$OUT/logs"
WORK_ROOT="$OUT/lhd_work"
ROCQ_DIR="$OUT/rocq"
LG_DIR="$OUT/lgdb"
RUNTIME_TMP_DIR="$OUT/runtime_tmp"

PRELUDE="$LIVEHD_ROOT/formal/rocq"

mkdir -p "$LOG_DIR" "$WORK_ROOT" "$ROCQ_DIR" "$LG_DIR" "$RUNTIME_TMP_DIR"

export TMPDIR="$RUNTIME_TMP_DIR"
export TMP="$RUNTIME_TMP_DIR"
export TEMP="$RUNTIME_TMP_DIR"

if [[ ! -x "$LHD" ]]; then
  echo "FATAL: missing lhd binary: $LHD" >&2
  echo "Run: cd $LIVEHD_ROOT && bazel build //lhd:lhd" >&2
  exit 2
fi

if [[ "$RUN_ROCQ" == "true" ]] && ! command -v "$ROCQ" >/dev/null 2>&1; then
  echo "FATAL: rocq not on PATH (set ROCQ= or RUN_ROCQ=false)." >&2
  echo "Install with: opam install rocq-prover   (then: eval \$(opam env))" >&2
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

design_dir_for() {
  case "$1" in
    SingleCycleCPU) echo "$SC_DIR" ;;
    PipelinedCPU) echo "$PIPE_DIR" ;;
    PipelinedDualIssueCPU) echo "$DUAL_DIR" ;;
    *) echo "" ;;
  esac
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
  echo "[pass.rocq] $top"
  set +e
  "$LHD" compile verilog \
    "${files[@]}" \
    --reader yosys-verilog \
    --top "$top" \
    --workdir "$work" \
    --result-json "$result" \
    --emit-dir lg:"$lg" \
    --emit-dir rocq:"$ROCQ_DIR" \
    --set yosys.setundef=zero \
    --set formal.rocq.strict="$STRICT" \
    --set formal.rocq.normalize=true \
    --set formal.rocq.emit_cert="$EMIT_CERT" \
    --set formal.rocq.max_width="$MAX_WIDTH" \
    --set formal.rocq.cert_wf="$CERT_WF" \
    --set formal.rocq.cert_wf_fallback="$CERT_WF_FALLBACK" \
    --set formal.rocq.cert_chunk_size="$CERT_CHUNK_SIZE" \
    --set formal.rocq.cert_chunk_limit="$CERT_CHUNK_LIMIT" \
    --set formal.rocq.eval_engine="$EVAL_ENGINE" \
    > "$log" 2>&1
  local status=$?
  set -e
  echo "  status: $status"
  if [[ "$status" -ne 0 ]]; then
    tail -80 "$log" >&2 || true
    exit "$status"
  fi

  local generated="$ROCQ_DIR/${top}_Lgraph.v"
  if [[ ! -r "$generated" ]]; then
    echo "FATAL: expected generated Rocq file missing: $generated" >&2
    exit 2
  fi
}

# Step 2: LEC frontend gate — prove RTL == LGraph before any prover generation.
if [[ "$RUN_LEC_GATE" == "true" ]]; then
  echo "[pipeline] step 2/5: LEC gate (RTL == LGraph) before pass.rocq"
  if ! LHD="$LHD" HAGENT="$HAGENT_BUILD" OUT="$OUT/lec_gate" LEC_STRICT="${LEC_STRICT:-false}" \
       bash "$SCRIPT_DIR/run_dino_lgraph_lec_gate.sh"; then
    echo "FATAL: LEC gate reported REFUTED (or strict INCONCLUSIVE); not generating Rocq" >&2
    exit 3
  fi
else
  echo "[pipeline] step 2/5: LEC gate SKIPPED (RUN_LEC_GATE=false)"
fi

echo "[pipeline] step 3/5: pass.rocq generation"
for top in $DESIGNS; do
  dir="$(design_dir_for "$top")"
  if [[ -z "$dir" ]]; then
    echo "FATAL: unknown design '$top' (expected one of SingleCycleCPU PipelinedCPU PipelinedDualIssueCPU)" >&2
    exit 2
  fi
  run_design "$top" "$dir"
done

# Step 4: typecheck.  The pass emits a _CoqProject with the design files and a
# `-Q . LiveHD` binding but no support-library root, because it does not know
# where this repository lives.  Prepend it here — same shape as the Isabelle
# runner generating its ROOT files at run time.
if [[ "$RUN_ROCQ" == "true" ]]; then
  echo "[pipeline] step 4/5: rocq typecheck"

  echo "  building the support library ($PRELUDE)"
  make -C "$PRELUDE" ROCQ="$ROCQ" > "$LOG_DIR/prelude_build.log" 2>&1 || {
    tail -40 "$LOG_DIR/prelude_build.log" >&2
    echo "FATAL: formal/rocq failed to build" >&2
    exit 4
  }

  proj="$ROCQ_DIR/_CoqProject"
  if [[ ! -f "$proj" ]]; then
    echo "FATAL: pass.rocq did not emit a _CoqProject in $ROCQ_DIR" >&2
    exit 4
  fi
  # Anchor on a real `-R` directive: the emitted file carries a COMMENT showing
  # the line to add, and a loose match would see that and skip the prepend.
  if ! grep -q '^-R .* RocqSemanticPrimitives$' "$proj"; then
    tmp="$proj.tmp"
    { echo "-R $PRELUDE/theories RocqSemanticPrimitives"; cat "$proj"; } > "$tmp"
    mv "$tmp" "$proj"
  fi

  (
    cd "$ROCQ_DIR"
    "$ROCQ" makefile -f _CoqProject -o Makefile.coq
    nice -n19 make -f Makefile.coq -j "$JOBS"
  ) > "$LOG_DIR/rocq_typecheck.log" 2>&1 || {
    tail -60 "$LOG_DIR/rocq_typecheck.log" >&2
    echo "FATAL: rocq typecheck failed; see $LOG_DIR/rocq_typecheck.log" >&2
    exit 4
  }
  echo "  typecheck OK"
else
  echo "[pipeline] step 4/5: rocq typecheck SKIPPED (RUN_ROCQ=false)"
fi

# Step 5 gate: no proof holes, no unsupported-construct markers.  A `sorry`-mode
# run (cert_wf=sorry) is expected to trip this — that is the point of the mode.
if grep -RIn "Admitted\|admit\.\|TODO pass\.rocq\|Memory node" "$ROCQ_DIR" >/dev/null 2>&1; then
  echo "WARNING: generated Rocq contains proof-trust or unsupported markers" >&2
  grep -RIn "Admitted\|admit\.\|TODO pass\.rocq\|Memory node" "$ROCQ_DIR" | head -80 >&2 || true
fi

echo "Generated DINO Rocq files: $ROCQ_DIR"
echo "Logs: $LOG_DIR"
