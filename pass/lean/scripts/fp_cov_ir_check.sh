#!/usr/bin/env bash
# IR-AS-SEMANTICS generation check + readiness, as a retained serial worker.
# Phases, kept separate and never regenerating already-emitted source:
#   1 elaborate <top>_Cert.lean      2 elaborate <top>_Main.lean (+ C)
#   3 readiness: run the launcher with nonzero simulator work
# The INTERPRETED route (lean --run) is used and labelled as such; it is not a
# native `lake exe` link.
#   fp_cov_ir_check.sh <row-id> <top> <cycles>
set -uo pipefail
R=/mada/users/czeng14/projects/livehd-pass-lean-master
COV=$R/generated/master_port/cov12x5
LEANBIN=/mada/users/czeng14/.elan/toolchains/leanprover--lean4---v4.31.0/bin/lean
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
[ "$(cat "$B/cert/exit" 2>/dev/null)" = 0 ] || { echo "$ROW: IR cert check FAILED"; exit 1; }

$CR "$ROW" ir_check_main "$B/main" 64G 1800 -- \
  env LEAN_PATH="$B/cert:$LP" "$LEANBIN" -o "$B/main/${TOP}_Main.olean" -c "$B/main/${TOP}_Main.c" "$S/Sims/${TOP}_Main.lean"
[ "$(cat "$B/main/exit" 2>/dev/null)" = 0 ] || { echo "$ROW: IR main check FAILED"; exit 1; }

$CR "$ROW" ir_readiness_run "$B/run" 64G 3600 -- \
  env LEAN_PATH="$B/cert:$LP" "$LEANBIN" --run "$S/Sims/${TOP}_Main.lean" "$TOP" --cycles "$CYC" --show-state
e=$(cat "$B/run/exit" 2>/dev/null || echo 99)
echo "$ROW IR readiness exit=$e lines=$(wc -l < "$B/run/stdout.log" 2>/dev/null || echo 0)"
