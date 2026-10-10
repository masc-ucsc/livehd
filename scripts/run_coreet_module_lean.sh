#!/usr/bin/env bash
set -uo pipefail

# CORE-ET (ETASP) per-module pass.lean runner.
#
# Same shape as run_cva6_module_lean_stress.sh, with three differences that
# matter:
#   * the filelist comes from core-et's own make variables (no bender, no sv2v,
#     no gate wrapper -- core-et modules have no struct-config parameters);
#   * `lhd pass single_edge` runs between compile and pass.lean, because
#     pass.lean fatals on Ntype_op::Latch (pass_lean.cpp:1064) and core-et's
#     tech_generic primitives are two-phase latch cells.  It is a no-op on a
#     plain posedge design (todo/livehd/2f-latch M8);
#   * the RTL-vs-LGraph LEC gate is wired in, which pass/lean/README.md calls
#     mandatory and the CVA6 runner omits.
#
# Usage:
#   COREET_TOP=intpipe_alu scripts/run_coreet_module_lean.sh
# Useful env: RUN_LEAN, RUN_LEC_GATE, LEC_STRICT, LEAN_EMIT_CERT,
#             LEAN_EMIT_FAST_BRIDGE, OUT, LEAN_JOBS, LEAN_CPUSET, STOP_AFTER

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIVEHD_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

COREET_ROOT="${COREET_ROOT:?set COREET_ROOT to the read-only CORE-ET checkout}"
TOP="${COREET_TOP:?set COREET_TOP=<module>}"
LHD="${LHD:-$LIVEHD_ROOT/bazel-bin/lhd/lhd}"
LAKE="${LAKE:-lake}"

RUN_LEAN="${RUN_LEAN:-false}"
# DEFAULT OFF. The gate works again (see the block below), but it is a real
# solver run and an export census does not want it. Off by default also
# means an export row is plainly an export rather than something that might
# or might not carry an equivalence result -- which is how 162 usage errors
# came to sit beside rows that looked validated. RUN_LEC_GATE=true now
# performs a real comparison and REFUSES to continue on anything but
# `proven`.
RUN_LEC_GATE="${RUN_LEC_GATE:-false}"
LEC_STRICT="${LEC_STRICT:-false}"
EMIT_CERT="${LEAN_EMIT_CERT:-true}"
EMIT_FAST_BRIDGE="${LEAN_EMIT_FAST_BRIDGE:-true}"
CERT_WF="${LEAN_CERT_WF:-skip}"
MAX_WIDTH="${LEAN_MAX_WIDTH:-1048576}"
# B1+B2 branch: `verified_compiler` makes pass.lean emit ONLY <Top>_designCert.
# It overrides emit_cert/emit_fast_bridge/cert_wf inside the pass, so the three
# knobs above become inert -- passing them anyway keeps this script's interface
# unchanged for the legacy mode.
LEAN_MODE="${LEAN_MODE:-legacy}"
LEAN_JOBS="${LEAN_JOBS:-8}"
LEAN_CPUSET="${LEAN_CPUSET:-0-7}"
# emit | single_edge | lec | lean  -- stop the pipeline early (census sweeps use `emit`)
STOP_AFTER="${STOP_AFTER:-}"

OUT="${OUT:-$LIVEHD_ROOT/generated/core-et/$TOP}"
LOG_DIR="$OUT/logs"
WORK_DIR="$OUT/lhd_work"
LG_RAW="$OUT/lgdb_raw"
LG_NORM="$OUT/lgdb_norm"
LEAN_DIR="$OUT/lean"
GEN_DIR="$OUT/generated_inputs"
RUNTIME_TMP="$OUT/runtime_tmp"
FILELIST="$LIVEHD_ROOT/generated/core-et/filelists/$TOP.f"
ANCHOR="$GEN_DIR/livehd_filelist_anchor.sv"

mkdir -p "$LOG_DIR" "$WORK_DIR" "$LG_RAW" "$LEAN_DIR" "$GEN_DIR" "$RUNTIME_TMP" \
         "$(dirname "$FILELIST")"
export TMPDIR="$RUNTIME_TMP" TMP="$RUNTIME_TMP" TEMP="$RUNTIME_TMP"

die() { echo "FATAL: $*" >&2; exit 2; }
[[ -x "$LHD" ]] || die "missing lhd binary: $LHD (bazel build //lhd:lhd)"
[[ "$EMIT_FAST_BRIDGE" != "true" || "$EMIT_CERT" == "true" ]] \
  || die "LEAN_EMIT_FAST_BRIDGE=true requires LEAN_EMIT_CERT=true"

# ---------------------------------------------------------------------------
# 0. filelist
# ---------------------------------------------------------------------------
COREET_ROOT="$COREET_ROOT" "$SCRIPT_DIR/coreet_filelist.sh" "$TOP" "$FILELIST" \
  > "$LOG_DIR/filelist.log" 2>&1 || { cat "$LOG_DIR/filelist.log" >&2; exit 2; }
n_src="$(grep -c '\.sv$' "$FILELIST" || true)"
[[ "$n_src" -gt 0 ]] || die "$TOP: filelist is empty ($FILELIST)"

printf '// Empty anchor. Real CORE-ET sources arrive via yosys.filelist_file.\n' > "$ANCHOR"

{
  echo "COREET_ROOT=$COREET_ROOT"; echo "TOP=$TOP"; echo "FILELIST=$FILELIST ($n_src files)"
  echo "OUT=$OUT"; echo "RUN_LEC_GATE=$RUN_LEC_GATE"; echo "RUN_LEAN=$RUN_LEAN"
  echo "LEAN_EMIT_CERT=$EMIT_CERT"; echo "LEAN_EMIT_FAST_BRIDGE=$EMIT_FAST_BRIDGE"
  echo "STOP_AFTER=$STOP_AFTER"
} > "$LOG_DIR/preflight.log"

# ---------------------------------------------------------------------------
# 1. RTL -> LGraph.  yosys-slang: the CVA6-proven reader, and its script runs an
#    unconditional `flatten`, which is what lets step 2 see the latch prims as
#    cells in the top graph instead of stateful Subs it would have to refuse.
#    --relax-enum-conversions is REQUIRED (92 typedef enum in core-et; mk/yosys.mk
#    passes it on every synthesis run).
# ---------------------------------------------------------------------------
"$LHD" compile verilog "$ANCHOR" \
  --reader yosys-slang --top "$TOP" \
  --workdir "$WORK_DIR" --result-json "$LOG_DIR/lhd_compile_result.json" \
  --emit-dir lg:"$LG_RAW" \
  --set yosys.filelist_file="$FILELIST" \
  --set yosys.setundef=zero \
  ${YOSYS_MEMORY_MODE:+--set yosys.memory_mode="$YOSYS_MEMORY_MODE"} \
  -- --ignore-assertions --relax-enum-conversions \
  > "$LOG_DIR/lhd_compile.log" 2>&1
status=$?
echo "compile exit=$status"
[[ "$status" -eq 0 ]] || { tail -30 "$LOG_DIR/lhd_compile.log"; exit "$status"; }
[[ "$STOP_AFTER" == "compile" ]] && exit 0

# ---------------------------------------------------------------------------
# 2. latches / negedge state -> posedge Flop (todo/livehd/2f-latch M8).
#    Skips itself on a plain posedge single-clock design, so this costs nothing
#    on the blocks that do not need it.  Fails closed: it declines a whole
#    design rather than half-transform it.
# ---------------------------------------------------------------------------
"$LHD" pass single_edge --top "$TOP" lg:"$LG_RAW" --emit-dir lg:"$LG_NORM" \
  --workdir "$WORK_DIR/se" > "$LOG_DIR/single_edge.log" 2>&1
se_status=$?
se_note="$(grep -oP '"message":"\K[^"]*' "$LOG_DIR/single_edge.log" | head -1)"
echo "single_edge exit=$se_status  ${se_note:-}"
if [[ "$se_status" -ne 0 ]]; then
  tail -20 "$LOG_DIR/single_edge.log"
  exit "$se_status"
fi
[[ "$STOP_AFTER" == "single_edge" ]] && exit 0

# ---------------------------------------------------------------------------
# 3. LEC gate: raw RTL == the normalized LGraph pass.lean will consume.
#    This is what lets steps 4-5 restrict their claim to "model = certificate"
#    instead of re-proving RTL semantics.  It is also the only thing that
#    catches the 2f-latch READERS bugs, which build a wrong graph silently.
# ---------------------------------------------------------------------------
if [[ "$RUN_LEC_GATE" == "true" ]]; then
  # RTL equivalence: the compiled LGraph against the raw RTL read INDEPENDENTLY.
  #
  # THE REFERENCE FRONTEND IS SLANG, NOT A YOSYS READER, and that is a real
  # difference worth stating rather than a detail. `lhd lec` permits source
  # inputs only through the default Slang reader (lhd/lhd_options.cpp:797 rejects
  # every other reader for a non-compile command); the implementation side here
  # was built by `inou.yosys.tolg` with yosys-slang. So the two sides come
  # through DIFFERENT front ends, which is what makes this a comparison rather
  # than a self-comparison.
  #
  # This block previously asked for `--reader yosys-slang` and set
  # `formal.strict`. Both are refused: the reader is compile-only, and
  # `formal.strict` is retired. Every invocation therefore exited 2 with
  # {"command":"usage","status":"fail"} -- 162 of 162 recorded gate logs on this
  # master are that usage error, and the shell called it "inconclusive" and
  # carried on. A gate that cannot run must never look like a gate that ran and
  # was unsure.
  #
  # Verified on this binary with a 4-bit adder: identical RTL gives exit 0
  # "verdict":"proven"; mutating `y` to `(a+b)^1` gives exit 10
  # "verdict":"refuted" with `counterexample: diff y(ref=1 impl=0) @ a=0, b=0`.
  #
  # ref side: the raw RTL, all modules CONCATENATED into one .sv and elaborated
  # independently. `--ref` takes a single path and `compile.yosys.filelist_file`
  # does not reach the ref read, so the filelist's include path has to be
  # re-supplied by hand (intpipe_csr_file's .svh set needs it).
  mapfile -t ref_srcs < <(grep '\.sv$' "$FILELIST")
  REF_SV="$GEN_DIR/raw_${TOP}.sv"
  cat "${ref_srcs[@]}" > "$REF_SV"
  declare -a ref_slang=(--ignore-assertions --relax-enum-conversions)
  if grep -q '+incdir+' "$FILELIST"; then
    ref_slang+=(-I "$(grep -m1 '^+incdir+' "$FILELIST" | sed 's/^+incdir+//')")
  fi
  rm -f "$LOG_DIR/lec_gate.json"
  "$LHD" lec --impl lg:"$LG_NORM" --ref verilog:"$REF_SV" --top "$TOP" \
    --workdir "$WORK_DIR/lec" \
    --set formal.engine=auto --set formal.lec.hier=true \
    --set formal.lec.semdiff=structural \
    --result-json "$LOG_DIR/lec_gate.json" \
    -- "${ref_slang[@]}" \
    > "$LOG_DIR/lec_gate.log" 2>&1
  lec_status=$?
  lec_verdict="$(grep -oP '"verdict":"\K[^"]*' "$LOG_DIR/lec_gate.json" 2>/dev/null | tail -1)"
  echo "lec gate exit=$lec_status verdict=${lec_verdict:-<none>}"

  # FOUR OUTCOMES, and only one of them is a pass. Collapsing them is how 162
  # usage errors came to sit beside rows that looked validated.
  if [[ ! -s "$LOG_DIR/lec_gate.json" ]]; then
    echo "FATAL: LEC produced no result JSON -- the gate did not run (usage or" >&2
    echo "       infrastructure failure, NOT an equivalence outcome). See" >&2
    echo "       $LOG_DIR/lec_gate.log" >&2
    exit 5
  fi
  case "$lec_verdict" in
    proven)
      echo "  LEC: PROVEN equivalent to the RTL" ;;
    refuted)
      echo "FATAL: LEC REFUTED -- the LGraph does not match the RTL; do NOT generate" >&2
      grep -m1 'counterexample' "$LOG_DIR/lec_gate.log" >&2 || true
      exit 4 ;;
    *)
      # Native UNKNOWN already exits nonzero on its own; do not soften it.
      echo "FATAL: LEC did not prove equivalence (verdict='${lec_verdict:-<none>}'," >&2
      echo "       exit $lec_status). An undecided gate is not a passed gate." >&2
      exit 6 ;;
  esac
fi
[[ "$STOP_AFTER" == "lec" ]] && exit 0

# ---------------------------------------------------------------------------
# 4. LGraph -> Lean fast model + certificate + step-5 fast-view bridge
# ---------------------------------------------------------------------------
"$LHD" compile lg:"$LG_NORM" --top "$TOP" \
  --workdir "$WORK_DIR/lean" --emit-dir lean:"$LEAN_DIR" \
  --result-json "$LOG_DIR/lhd_lean_result.json" \
  --set formal.lean.strict=true \
  --set formal.lean.emit_cert="$EMIT_CERT" \
  --set formal.lean.emit_fast_bridge="$EMIT_FAST_BRIDGE" \
  --set formal.lean.cert_wf="$CERT_WF" \
  --set formal.lean.max_width="$MAX_WIDTH" \
  --set formal.lean.mode="$LEAN_MODE" \
  > "$LOG_DIR/lhd_lean.log" 2>&1
lean_emit_status=$?
echo "lean emit exit=$lean_emit_status"
if [[ "$lean_emit_status" -ne 0 ]]; then
  grep -oP '"code":"\K[^"]*' "$LOG_DIR/lhd_lean.log" | sort -u | sed 's/^/  diag: /'
  tail -10 "$LOG_DIR/lhd_lean.log"
  exit "$lean_emit_status"
fi

generated="$LEAN_DIR/${TOP}_Lgraph.lean"
[[ -r "$generated" ]] || die "no generated Lean file: $generated"

# ---------------------------------------------------------------------------
# 5. Static gates (seconds).  These run BEFORE any typecheck: they cost nothing
#    and replace hours of discovery.  pass/lean/README.md "Static gates first".
# ---------------------------------------------------------------------------
gate_status=0
if [[ "$LEAN_MODE" == "verified_compiler" ]]; then
  # Different output, different gates.  op_census.py and const_parity.py read the
  # LEGACY shape (`nodes_of_list` / `BT.nd` trees, per-node fast bodies); a
  # verified_compiler file has none of that, so they report 0 nodes and fail.
  #
  # DO NOT reimplement these inline.  This block used to be a hand-rolled copy of
  # pass/lean/scripts/vc_gates.py, and the copy went stale in both directions:
  #   * it refused any certificate with zero NODES, which is wrong -- a design
  #     whose outputs are all constant-driven is valid and provable.  That gate
  #     alone blocked `null_vpu` and `minion_dcache_texsend`, both of which had
  #     emitted complete four-theorem certificates.
  #   * it was MISSING the const-width and ROM-entry gates, so the CORE-ET sweep
  #     ran weaker checks than the CVA6 sweep against the same emitter.  Four
  #     modules reached a green PROVEN carrying a constant whose declared width
  #     cannot hold its value.  That is a flag, NOT a confirmed defect:
  #     CertificateBuilder::dep deliberately sets a constant source to the
  #     CONSUMER's width, so a narrow constant can be correct modular
  #     arithmetic.  The gate still has to run, because if one of them IS a real
  #     truncation then native_decide proves it faithfully against a model that
  #     is not the RTL -- the one failure nothing downstream can detect.
  # One implementation, called from both drivers.
  python3 -I "$LIVEHD_ROOT/pass/lean/scripts/vc_gates.py" "$generated" "$TOP" \
    > "$LOG_DIR/static_gates.log" 2>&1 || gate_status=1
  echo "gate_status=$gate_status" >> "$LOG_DIR/static_gates.log"
else
{
  echo "== op census =="
  python3 "$LIVEHD_ROOT/pass/lean/scripts/op_census.py" "$generated" || gate_status=1
  if [[ "$EMIT_FAST_BRIDGE" == "true" ]]; then
    echo "== const parity =="
    python3 "$LIVEHD_ROOT/pass/lean/scripts/const_parity.py" "$generated" || gate_status=1
    echo "== sorry / TODO(step5) =="
    n_sorry="$(grep -cw sorry "$generated" || true)"
    n_todo="$(grep -c 'TODO(step5)' "$generated" || true)"
    echo "sorry=$n_sorry TODO(step5)=$n_todo"
    [[ "$n_sorry" == "0" && "$n_todo" == "0" ]] || gate_status=1
    echo "== _refines_fast theorems =="
    grep -oE '^theorem [A-Za-z0-9_]+_(comb|next|step)_refines_fast' "$generated" || true
    grep -q "_comb_refines_fast" "$generated" || gate_status=1
  fi
  echo "gate_status=$gate_status"
} > "$LOG_DIR/static_gates.log" 2>&1
fi
echo "static gates: gate_status=$gate_status ($LOG_DIR/static_gates.log)"
grep -E '^(PASS|FAIL|cert nodes|state fields|node output widths|sources=|sorry=)' "$LOG_DIR/static_gates.log" | head -8
[[ "$gate_status" -eq 0 ]] || { echo "FATAL: static gates failed; not starting a typecheck" >&2; exit 3; }
[[ "$STOP_AFTER" == "lean" ]] && exit 0

# ---------------------------------------------------------------------------
# 6. Typecheck.  Prefer scripts/run_lean_queue.sh for real runs (it serializes
#    and applies the axiom gate); this inline path is for one-offs.
# ---------------------------------------------------------------------------
if [[ "$RUN_LEAN" == "true" ]]; then
  (
    cd "$LIVEHD_ROOT/formal/lean" || exit 2
    LEAN_NUM_THREADS="$LEAN_JOBS" taskset -c "$LEAN_CPUSET" nice -n 19 ionice -c 3 \
      /usr/bin/time -f 'WALL=%e s PEAK_RSS=%M KB' stdbuf -oL "$LAKE" env lean "$generated"
  ) > "$LOG_DIR/lean_typecheck.log" 2>&1
  lean_status=$?
  echo "lean_exit=$lean_status" >> "$LOG_DIR/lean_typecheck.log"
  echo "lean typecheck exit=$lean_status"
  grep -E 'error:|WALL=' "$LOG_DIR/lean_typecheck.log" | head -20
  [[ "$lean_status" -eq 0 ]] || exit "$lean_status"
fi

echo "OK  $TOP  -> $LEAN_DIR"
