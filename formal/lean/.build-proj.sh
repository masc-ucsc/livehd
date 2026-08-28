#!/usr/bin/env bash
# Build the Projection modules with plain `lean` (no lake, no Mathlib).
# Only OpBridge.lean needs Mathlib; nothing here does, so we skip the hours-long
# Mathlib build entirely and keep iteration at a few seconds.
set -uo pipefail
cd "$(dirname "$0")"
OUT=.olean-dev
export LEAN_PATH="$PWD/$OUT"
export LEAN_NUM_THREADS=4
mkdir -p "$OUT/LeanSemanticPrimitives/Projection"
# Default: the whole Projection library, in dependency order.
MODULES=(ObjectLanguage ObjectLanguageSemantics Encoding BindingTime
         PartialEvaluator Surface BTA MixProgram Demo Gate0
         PartialEvaluatorCorrect Audit)
if [ $# -gt 0 ]; then MODULES=("$@"); fi

rc=0
for m in "${MODULES[@]}"; do
  [ -f "LeanSemanticPrimitives/Projection/$m.lean" ] || continue
  src="LeanSemanticPrimitives/Projection/$m.lean"
  olean="$OUT/LeanSemanticPrimitives/Projection/$m.olean"
  printf '  %-28s ' "$m"
  t0=$(date +%s%N)
  if out=$(taskset -c 0-3 nice -n 19 lean -o "$olean" "$src" 2>&1); then
    t1=$(date +%s%N); printf 'ok   %5s ms\n' "$(( (t1-t0)/1000000 ))"
    [ -n "$out" ] && echo "$out" | sed 's/^/      /'
  else
    t1=$(date +%s%N); printf 'FAIL %5s ms\n' "$(( (t1-t0)/1000000 ))"
    echo "$out" | head -25 | sed 's/^/      /'
    rc=1
  fi
done
exit $rc
