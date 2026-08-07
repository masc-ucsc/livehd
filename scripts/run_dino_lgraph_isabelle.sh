#!/usr/bin/env bash
set -euo pipefail

# Generate Isabelle LGraph models + certificates for the three DINO CPUs.
#
# The Isabelle analogue of scripts/run_dino_lgraph_lean.sh.  Validation pipeline
# (order matters):
#   1. LiveHD compile   RTL -> LGraph
#   2. LEC gate         prove/classify RTL == LGraph   (run_dino_lgraph_lec_gate.sh)
#                       REFUTED aborts; INCONCLUSIVE warns (LEC_STRICT=true = hard)
#   3. pass.isabelle    LGraph -> <Top>_Lgraph.thy + <Top>_Lgraph_Cert.thy
#   4. isabelle build   typecheck the model (and cert) sessions   (RUN_ISABELLE=true)
#   5. cert bridge      generated fast model = graph certificate  (pass/isabelle/BRIDGE_BUGS.md)
#
# The LEC gate matters more here than it looks: certificate equivalence is
# self-consistent by construction, so it CANNOT catch an emitter semantic bug --
# when the fast model and the certificate share a defect they still agree with
# each other (see pass/isabelle/TODO:43-48 for the Get_mask case).  Only LEC
# catches that class.
#
# This replaces the livehd-proof copy, which drove the removed `lgshell` REPL.
# Everything stays under generated/ so shared machines never depend on /tmp.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIVEHD_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

LHD="${LHD:-$LIVEHD_ROOT/bazel-bin/lhd/lhd}"
ISABELLE="${ISABELLE:-/soe/czeng14/.local/Isabelle2025-2/bin/isabelle}"

RUN_ISABELLE="${RUN_ISABELLE:-true}"
RUN_LEC_GATE="${RUN_LEC_GATE:-false}"
STRICT="${ISA_STRICT:-true}"
MAX_WIDTH="${ISA_MAX_WIDTH:-1024}"
CERT_WF_MODE="${CERT_WF_MODE:-skip}"
CERT_WF_FALLBACK="${CERT_WF_FALLBACK:-fail}"
CERT_CHUNK_SIZE="${CERT_CHUNK_SIZE:-25}"
CERT_CHUNK_LIMIT="${CERT_CHUNK_LIMIT:-0}"

# Which designs to run.  Default is SingleCycleCPU only: it is the direct
# analogue of the Lean milestone and by far the cheapest of the three
# (the DualIssue model theory alone took 18-21 min single-threaded).
DESIGNS="${DESIGNS:-SingleCycleCPU}"

# Shared NFS server: cap cores and stay nice.  parallel_proofs defaults ON here
# (the livehd-proof script pinned it to 0 / threads=1), because the certificate
# bridge emits one independent lemma per node and that is exactly the shape
# Isabelle can parallelize.  Set PARALLEL_PROOFS=0 to reproduce the old numbers.
THREADS="${THREADS:-8}"
PARALLEL_PROOFS="${PARALLEL_PROOFS:-1}"
NICE=(nice -n 19 ionice -c 3)

HAGENT_BUILD="${HAGENT_BUILD:-/mada/users/czeng14/projects/hagent/.cache/setup_simplechisel_mcp_2025.11/build}"
SC_DIR="${SC_DIR:-$HAGENT_BUILD/build_singlecyclecpu_d}"
PIPE_DIR="${PIPE_DIR:-$HAGENT_BUILD/build_pipelined_d}"
DUAL_DIR="${DUAL_DIR:-$HAGENT_BUILD/build_dualissue_d}"

OUT="${OUT:-$LIVEHD_ROOT/generated/dino_lgraph_isabelle}"
ISA_DIR="$OUT/isabelle"
LOG_DIR="$OUT/logs"
WORK_ROOT="$OUT/lhd_work"
LG_DIR="$OUT/lgdb"
HOME_DIR="$OUT/isabelle_home"
RUNTIME_TMP_DIR="$OUT/runtime_tmp"

mkdir -p "$ISA_DIR" "$LOG_DIR" "$WORK_ROOT" "$LG_DIR" "$HOME_DIR" "$RUNTIME_TMP_DIR"

# Project-local scratch for anything that goes through TMPDIR.  Poly/ML in
# particular reports "I/O error: Operation not permitted" -- with no theory,
# proof, or type error -- when it cannot write its scratch files.  That is a
# filesystem problem, not a failed proof.
export TMPDIR="$RUNTIME_TMP_DIR"
export TMP="$RUNTIME_TMP_DIR"
export TEMP="$RUNTIME_TMP_DIR"

if [[ ! -x "$LHD" ]]; then
  echo "FATAL: missing lhd binary: $LHD" >&2
  echo "Run: cd $LIVEHD_ROOT && bazel build //lhd:lhd" >&2
  exit 2
fi
if [[ "$RUN_ISABELLE" == "true" && ! -x "$ISABELLE" ]]; then
  echo "FATAL: missing isabelle binary: $ISABELLE" >&2
  echo "Override with ISABELLE=/path/to/isabelle" >&2
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

dir_for_top() {
  case "$1" in
    SingleCycleCPU)         echo "$SC_DIR" ;;
    PipelinedCPU)           echo "$PIPE_DIR" ;;
    PipelinedDualIssueCPU)  echo "$DUAL_DIR" ;;
    *) echo "FATAL: unknown design '$1'" >&2; exit 2 ;;
  esac
}

run_design() {
  local top="$1"
  local dir
  dir="$(dir_for_top "$top")"
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
  echo "[pass.isabelle] $top"
  set +e
  "${NICE[@]}" "$LHD" compile verilog \
    "${files[@]}" \
    --reader yosys-verilog \
    --top "$top" \
    --workdir "$work" \
    --result-json "$result" \
    --emit-dir lg:"$lg" \
    --emit-dir isabelle:"$ISA_DIR" \
    --set yosys.setundef=zero \
    --set formal.isabelle.strict="$STRICT" \
    --set formal.isabelle.normalize=true \
    --set formal.isabelle.max_width="$MAX_WIDTH" \
    --set formal.isabelle.cert_wf="$CERT_WF_MODE" \
    --set formal.isabelle.cert_wf_fallback="$CERT_WF_FALLBACK" \
    --set formal.isabelle.cert_chunk_size="$CERT_CHUNK_SIZE" \
    --set formal.isabelle.cert_chunk_limit="$CERT_CHUNK_LIMIT" \
    > "$log" 2>&1
  local status=$?
  set -e
  echo "  status: $status"
  if [[ "$status" -ne 0 ]]; then
    tail -80 "$log" >&2 || true
    exit "$status"
  fi
}

session_name_for_top() {
  case "$1" in
    SingleCycleCPU)         echo "DINO-Lgraph-SingleCycle" ;;
    PipelinedCPU)           echo "DINO-Lgraph-Pipelined" ;;
    PipelinedDualIssueCPU)  echo "DINO-Lgraph-DualIssue" ;;
  esac
}

# One session pair per design: <name>-Model (the fast word model) and
# <name>-Cert (the certificate, which imports it).  Keeping them separate means
# a cert failure does not force re-elaborating the model theory, which is the
# expensive half today.
emit_sessions() {
  local sess_dir="$ISA_DIR/sessions"
  rm -rf "$sess_dir"
  : > "$ISA_DIR/ROOTS"
  rm -f "$ISA_DIR/ROOT"

  local top base model_dir cert_dir
  for top in $DESIGNS; do
    base="$(session_name_for_top "$top")"
    model_dir="$sess_dir/${top}_model"
    cert_dir="$sess_dir/${top}_cert"
    mkdir -p "$model_dir" "$cert_dir"

    ln -sf "../../${top}_Lgraph.thy" "$model_dir/${top}_Lgraph.thy"
    # The cert theory imports the model by bare name; inside a session pair it
    # has to be qualified with the model session.
    sed "s|^  imports ${top}_Lgraph |  imports \"${base}-Model.${top}_Lgraph\" |" \
      "$ISA_DIR/${top}_Lgraph_Cert.thy" > "$cert_dir/${top}_Lgraph_Cert.thy"

    cat > "$model_dir/ROOT" <<EOF
session "${base}-Model" = "LGraph-Translation-Correctness" +
  options [document = false, browser_info = false, parallel_proofs = ${PARALLEL_PROOFS}]
  theories
    ${top}_Lgraph
EOF

    cat > "$cert_dir/ROOT" <<EOF
session "${base}-Cert" = "${base}-Model" +
  options [document = false, browser_info = false, parallel_proofs = ${PARALLEL_PROOFS}]
  theories
    ${top}_Lgraph_Cert
EOF

    printf 'sessions/%s_model\nsessions/%s_cert\n' "$top" "$top" >> "$ISA_DIR/ROOTS"
  done
}

if [[ "$RUN_LEC_GATE" == "true" ]]; then
  echo "[pipeline] step 2/5: LEC gate (RTL == LGraph) before pass.isabelle"
  if ! LHD="$LHD" HAGENT="$HAGENT_BUILD" OUT="$OUT/lec_gate" LEC_STRICT="${LEC_STRICT:-false}" \
       bash "$SCRIPT_DIR/run_dino_lgraph_lec_gate.sh"; then
    echo "FATAL: LEC gate reported REFUTED (or strict INCONCLUSIVE); not generating Isabelle" >&2
    exit 3
  fi
else
  echo "[pipeline] step 2/5: LEC gate SKIPPED (RUN_LEC_GATE=false)"
fi

echo "[pipeline] step 3/5: pass.isabelle generation (designs: $DESIGNS)"
for top in $DESIGNS; do
  run_design "$top"
done

emit_sessions

if grep -RIn "TODO pass.isabelle\|big const\|undefined" "$ISA_DIR"/*.thy >/dev/null 2>&1; then
  echo "FATAL: generated Isabelle contains unresolved pass.isabelle markers" >&2
  grep -RIn "TODO pass.isabelle\|big const\|undefined" "$ISA_DIR"/*.thy | head -80 >&2
  exit 1
fi
if grep -RIn "sorry" "$ISA_DIR"/*.thy >/dev/null 2>&1; then
  echo "WARNING: generated Isabelle contains 'sorry' (expected today for cert_wf:chunked" >&2
  echo "         via <Top>_cert_all_ids_distinct; see pass/isabelle/BRIDGE_BUGS.md)" >&2
  grep -RIn "sorry" "$ISA_DIR"/*.thy | head -20 >&2 || true
fi

if [[ "$RUN_ISABELLE" == "true" ]]; then
  echo "[pipeline] step 4/5: isabelle build (threads=$THREADS parallel_proofs=$PARALLEL_PROOFS)"
  for top in $DESIGNS; do
    base="$(session_name_for_top "$top")"
    for suffix in Model Cert; do
      echo "[build] ${base}-${suffix}"
      set +e
      HOME="$HOME_DIR" "${NICE[@]}" "$ISABELLE" build -v \
        -o document=false \
        -o browser_info=false \
        -o "threads=$THREADS" \
        -d "$LIVEHD_ROOT/formal/semantic_primitives" \
        -d "$LIVEHD_ROOT/formal/translation_correctness" \
        -d "$ISA_DIR" \
        "${base}-${suffix}" \
        > "$LOG_DIR/${top}_${suffix}.build.log" 2>&1
      status=$?
      set -e
      echo "  status: $status  (log: $LOG_DIR/${top}_${suffix}.build.log)"
      if [[ "$status" -ne 0 ]]; then
        tail -60 "$LOG_DIR/${top}_${suffix}.build.log" >&2 || true
        exit "$status"
      fi
      grep -E "^Finished|elapsed time" "$LOG_DIR/${top}_${suffix}.build.log" | tail -2 || true
    done
  done
else
  echo "[pipeline] step 4/5: isabelle build SKIPPED (RUN_ISABELLE=false)"
fi

echo
echo "Generated Isabelle theories: $ISA_DIR"
echo "Sessions:                    $ISA_DIR/sessions"
echo "Logs:                        $LOG_DIR"
