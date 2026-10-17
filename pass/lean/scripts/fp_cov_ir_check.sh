#!/usr/bin/env bash
# IR-AS-SEMANTICS generation check + readiness, as a retained serial worker.
# Phases, kept separate and never regenerating already-emitted source:
#   1 elaborate <top>_Cert.lean      2 elaborate <top>_Main.lean (+ C)
#   3 readiness: run the launcher with nonzero simulator work
# The INTERPRETED route (lean --run) is used and labelled as such; it is not a
# native `lake exe` link.
#
#   fp_cov_ir_check.sh <row-id> <top> <cycles>     run the three phases
#   fp_cov_ir_check.sh --classify <row-id>         read-only: re-derive the
#                                                  disposition of a RETAINED
#                                                  ir_checks tree, running
#                                                  nothing.
#
# EXIT PROPAGATION (defect fixed 2026-10-11): every phase's true producer exit
# is read back from the cell's `exit` file AND from GNU time's `exit %x`, the
# two are required to agree, and the worker exits with the real run code instead
# of the 0 that a trailing `echo` used to return.  Outcomes are classified, so a
# timeout cap (124), a producer refusal (other nonzero) and a phase that never
# produced an exit record (MISSING -> 97) are distinguishable from OK.
set -uo pipefail
R=/WORKROOT/projects/livehd-pass-lean-master
COV=$R/generated/master_port/cov12x5
LEANBIN=/WORKROOT/.elan/toolchains/leanprover--lean4---v4.31.0/bin/lean

# classify <cell-dir> <phase-name>  ->  prints "<label> exit=<e> time_exit=<t>"
#   OK | CAP | REFUSED | MISSING | DISAGREE
classify() {
  local d=$1 ph=$2 e t lab
  if [ ! -f "$d/exit" ]; then
    echo "MISSING exit=- time_exit=- phase=$ph (no exit record retained)"; return 97
  fi
  e=$(cat "$d/exit")
  t=$(awk '$1=="exit"{print $2}' "$d/time.txt" 2>/dev/null)
  [ -n "$t" ] || t="-"
  if [ "$t" != "-" ] && [ "$t" != "$e" ]; then
    # wrapper rc and the measured producer rc disagree -> never call this a pass
    echo "DISAGREE exit=$e time_exit=$t phase=$ph (wrapper and producer differ)"; return 96
  fi
  case "$e" in
    0)   lab=OK ;;
    124) lab=CAP ;;        # `timeout` fired: partial, not a refusal and not a pass
    *)   lab=REFUSED ;;    # producer itself failed
  esac
  echo "$lab exit=$e time_exit=$t phase=$ph"
  return "$e"
}

# ---- read-only classification of a retained tree -------------------------
if [ "${1:-}" = "--classify" ]; then
  ROW=${2:?row}; B=$COV/$ROW/ir_checks
  [ -d "$B" ] || { echo "$ROW: no retained ir_checks tree at $B"; exit 95; }
  worst=0
  for ph in cert main run; do
    out=$(classify "$B/$ph" "$ph"); c=$?
    printf '%-28s %-18s %s\n' "$ROW" "$ph" "$out"
    [ "$c" != 0 ] && worst=$c
  done
  if [ -f "$B/run/stdout.log" ]; then
    echo "$ROW run stdout lines=$(wc -l < "$B/run/stdout.log")"
  fi
  echo "$ROW DISPOSITION exit=$worst (0 = all three phases OK)"
  exit "$worst"
fi

# ---- the three phases ----------------------------------------------------
ROW=${1:?row}; TOP=${2:?top}; CYC=${3:-8}
S=$COV/$ROW/ir_as_semantics/tree/formal/lean
B=$COV/$ROW/ir_checks
mkdir -p "$COV/tmp"; mkdir "$B" 2>/dev/null || { echo "$ROW: ir_checks exists; refusing to reuse"; exit 3; }
export TMPDIR=$COV/tmp TMP=$COV/tmp TEMP=$COV/tmp
LP=$(cd "$R/formal/lean" && lake env printenv LEAN_PATH 2>/dev/null)
CR="bash $R/pass/lean/scripts/fp_cov_run.sh"
{ echo "row=$ROW top=$TOP cycles=$CYC"; echo "cert=$S/Sims/${TOP}_Cert.lean sha256=$(sha256sum "$S/Sims/${TOP}_Cert.lean" | cut -d' ' -f1)"
  echo "main=$S/Sims/${TOP}_Main.lean sha256=$(sha256sum "$S/Sims/${TOP}_Main.lean" | cut -d' ' -f1)"
  echo "lean=$LEANBIN version=$($LEANBIN --version)"; echo "route=INTERPRETED (lean --run); NOT a native lake exe link"
  echo "LEAN_PATH=$LP"; } > "$B/BINDING.txt"

mkdir -p "$B/cert/Sims"
$CR "$ROW" ir_check_cert "$B/cert" 64G 5400 -- \
  env LEAN_PATH="$LP" "$LEANBIN" -o "$B/cert/Sims/${TOP}_Cert.olean" "$S/Sims/${TOP}_Cert.lean"
out=$(classify "$B/cert" cert); c=$?
echo "$ROW cert $out"
[ "$c" = 0 ] || { echo "$ROW IR DISPOSITION exit=$c (stopped at cert)" | tee "$B/DISPOSITION.txt"; exit "$c"; }

$CR "$ROW" ir_check_main "$B/main" 64G 1800 -- \
  env LEAN_PATH="$B/cert:$LP" "$LEANBIN" -o "$B/main/${TOP}_Main.olean" -c "$B/main/${TOP}_Main.c" "$S/Sims/${TOP}_Main.lean"
out=$(classify "$B/main" main); c=$?
echo "$ROW main $out"
[ "$c" = 0 ] || { echo "$ROW IR DISPOSITION exit=$c (stopped at main)" | tee "$B/DISPOSITION.txt"; exit "$c"; }

$CR "$ROW" ir_readiness_run "$B/run" 64G 3600 -- \
  env LEAN_PATH="$B/cert:$LP" "$LEANBIN" --run "$S/Sims/${TOP}_Main.lean" "$TOP" --cycles "$CYC" --show-state
out=$(classify "$B/run" run); c=$?
echo "$ROW run $out lines=$(wc -l < "$B/run/stdout.log" 2>/dev/null || echo 0)"
echo "$ROW IR DISPOSITION exit=$c" | tee "$B/DISPOSITION.txt"
# propagate the TRUE run result: a trailing echo must not turn a cap or a
# refusal into a 0 for whatever launched this worker.
exit "$c"
