#!/usr/bin/env bash
# SERIAL COVERAGE QUEUE, CORE-ET rows, APPROACH-MAJOR (one approach per sweep).
# Route: coreet_filelist.sh (make-expanded, whole unit) -> lhd compile verilog
# -> pass single_edge -> the approach's own emit.  Every phase gates on the
# PRODUCER'S EXIT plus a pass status plus a real library; attempt directories
# are immutable.
#   fp_cov_queue_coreet.sh <row-id> <unit> <approach>
set -uo pipefail
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"   # the repo root, from this script's location
COV=$R/generated/master_port/cov12x5
SNAP=$R/generated/master_port/sim_inputs/pinned
LOPT=${FP_LHD:-$(readlink -f "$R/bazel-bin/lhd/lhd")}   # bound by realpath; its sha256 is recorded below
ROW=${1:?row}; UNIT=${2:?unit}; APP=${3:-legacy}
case "$APP" in legacy|verified_compiler) ;; *) echo "unknown approach: $APP"; exit 2 ;; esac
ATTEMPT=${ATTEMPT:-$(date +%Y%m%dT%H%M%S)-$$}
B=$COV/$ROW/attempt-$ATTEMPT
# ATOMIC exclusive creation: plain mkdir fails if the directory exists, so a
# colliding ATTEMPT is refused rather than silently reused (mkdir -p would).
mkdir -p "$COV/tmp" "$(dirname "$B")"
mkdir "$B" 2>/dev/null || { echo "$ROW: ATTEMPT COLLISION, $B exists; refusing to reuse"; exit 3; }
export TMPDIR=$COV/tmp TMP=$COV/tmp TEMP=$COV/tmp COREET_ROOT=${COREET_ROOT:?set COREET_ROOT to the pinned core-et checkout}
# FP_SINGLE_EDGE_SET: one `--set` for pass.single_edge, e.g. multi_clock=true for
# a design with genuinely unrelated clocks (B05); recorded in BINDING.txt.
# No wall-clock cap by default (FP_TIMEOUT=0 disables GNU timeout); the memory
# ceiling (FP_MEMMAX, default 48G) stays, for host safety only.
CR="bash $R/pass/lean/scripts/fp_cov_run.sh"
# Parse the TOP-LEVEL status field, never a "pass" substring that a phase name
# or a diagnostic could carry.
json_status() { python3 -I -c "import json,sys;print((json.load(open(sys.argv[1])) or {}).get('status',''))" "$1" 2>/dev/null; }
# Confirm the INTENDED top is actually in the emitted library.
# The library record format is `graph_io <numeric_id> <name>`; match the NAME
# field exactly rather than any occurrence of the string, which can appear in a
# port name or a comment.
has_top() { awk -v want="$2" '$1=="graph_io" && $3==want {found=1} END{exit !found}' "$1/library.txt" 2>/dev/null; }

FL=$B/$UNIT.f
# FP_FILELIST: a PREPARED adapter filelist (e.g. coreet_icache_input.py's
# explicit-import input for B03) used instead of coreet_filelist.sh.  It is
# copied into the attempt and recorded by sha256; FP_INCLUDE_DIR adds its -I.
if [ -n "${FP_FILELIST:-}" ]; then
  [ -r "$FP_FILELIST" ] || { echo "$ROW/$ATTEMPT: NO FILELIST $FP_FILELIST"; exit 2; }
  cp "$FP_FILELIST" "$FL"
  echo "adapter filelist $FP_FILELIST sha256=$(sha256sum "$FP_FILELIST" | cut -d' ' -f1)" > "$B/filelist.log"
elif ! timeout 600 bash "$SNAP/IR_Semantics/scripts/coreet_filelist.sh" "$UNIT" "$FL" > "$B/filelist.log" 2>&1; then
  echo "$ROW/$ATTEMPT: BLOCKED at filelist -- $(tail -1 "$B/filelist.log")"; exit 2
fi
INC=$(grep -m1 "^+incdir+" "$FL" | sed "s/^+incdir+/-I /")
[ -n "${FP_INCLUDE_DIR:-}" ] && INC="-I $FP_INCLUDE_DIR"
n=$(grep -c '^/' "$FL" || true)
[ "$n" -gt 0 ] || { echo "$ROW/$ATTEMPT: BLOCKED, empty filelist"; exit 2; }

# The positional source is an ANCHOR .sv; the real sources arrive through
# yosys.filelist_file.  Passing the .f itself made slang parse the filelist as
# SystemVerilog ("expected member", "expected a declaration name").
ANCHOR=$B/anchor.sv
printf '// Empty anchor. Real CORE-ET sources arrive via yosys.filelist_file.\n' > "$ANCHOR"
# PRE-LAUNCH provenance binding: written BEFORE any phase executes.
{ echo "row=$ROW attempt=$ATTEMPT unit=$UNIT approach=$APP"
  echo "filelist=$FL sha256=$(sha256sum "$FL" | cut -d' ' -f1) files=$n"
  echo "lhd=$LOPT sha256=$(sha256sum "$LOPT" | cut -d' ' -f1)"
  echo "coreet_root=$COREET_ROOT rev=$(git -C $COREET_ROOT rev-parse HEAD)"
  echo "max_width=1048576 (default 1024 refusal preserved)"
  echo "adapter_filelist=${FP_FILELIST:-none} include=${INC:-none}"
  echo "single_edge_set=${FP_SINGLE_EDGE_SET:-none}"; } > "$B/BINDING.txt"

$CR "$ROW" s1_compile_lg "$B/s1_compile" "${FP_MEMMAX:-48G}" "${FP_TIMEOUT:-0}" -- \
  "$LOPT" compile verilog "$ANCHOR" --reader yosys-slang --top "$UNIT" \
  --workdir "$B/s1_compile/work" --result-json "$B/s1_compile/result.json" \
  --emit-dir lg:"$B/s1_compile/lg" --set yosys.filelist_file="$FL" \
  --set yosys.setundef=zero -- --ignore-assertions --relax-enum-conversions $INC
s1=$(cat "$B/s1_compile/exit" 2>/dev/null || echo 99)
{ [ "$s1" = 0 ] && [ "$(json_status "$B/s1_compile/result.json")" = pass ] \
  && [ -s "$B/s1_compile/lg/library.txt" ] && has_top "$B/s1_compile/lg" "$UNIT"; } \
  || { echo "$ROW/$ATTEMPT: STOP after s1 (exit $s1)"; exit 1; }

$CR "$ROW" s2_single_edge "$B/s2_single_edge" "${FP_MEMMAX:-48G}" "${FP_TIMEOUT:-0}" -- \
  "$LOPT" pass single_edge --top "$UNIT" lg:"$B/s1_compile/lg" ${FP_SINGLE_EDGE_SET:+--set "$FP_SINGLE_EDGE_SET"} \
  --emit-dir lg:"$B/s2_single_edge/lg" --workdir "$B/s2_single_edge/work" \
  --result-json "$B/s2_single_edge/result.json"
s2=$(cat "$B/s2_single_edge/exit" 2>/dev/null || echo 99)
{ [ "$s2" = 0 ] && [ "$(json_status "$B/s2_single_edge/result.json")" = pass ] \
  && [ -s "$B/s2_single_edge/lg/library.txt" ] && has_top "$B/s2_single_edge/lg" "$UNIT"; } \
  || { echo "$ROW/$ATTEMPT: STOP after single_edge (exit $s2)"; exit 1; }

case "$APP" in
  legacy)            OPTS="--set formal.lean.emit_cert=true --set formal.lean.cert_wf=eval" ;;
  verified_compiler) OPTS="--set formal.lean.mode=verified_compiler" ;;
  *) echo "unknown approach $APP"; exit 2 ;;
esac
$CR "$ROW" "$APP" "$B/$APP" "${FP_MEMMAX:-48G}" "${FP_TIMEOUT:-0}" -- \
  "$LOPT" compile lg:"$B/s2_single_edge/lg" --top "$UNIT" \
  --workdir "$B/$APP/work" --result-json "$B/$APP/result.json" \
  --emit-dir lean:"$B/$APP/lean" $OPTS --set formal.lean.max_width=1048576
e=$(cat "$B/$APP/exit" 2>/dev/null || echo 99)
if [ "$e" = 0 ] && [ -s "$B/$APP/lean/${UNIT}_Lgraph.lean" ]; then
  echo "$ROW/$ATTEMPT $APP EMITTED $(stat -c%s "$B/$APP/lean/${UNIT}_Lgraph.lean") B"
else
  echo "$ROW/$ATTEMPT $APP NO (exit $e)"
fi
