#!/usr/bin/env bash
# Build the shared semantic core + the Projection modules with plain `lean`
# (no lake, no Mathlib).
#
# The shared core -- SemanticPrimitives, Translation/LGraphModel,
# Translation/GraphRefine, Compiler/* -- is imported verbatim from livehd-new at
# a pinned revision (see Projection/SHARED_SEMANTICS.md).  None of it imports
# Mathlib, so it builds here in seconds alongside everything else.
#
# Translation/OpBridge.lean DOES need Mathlib and is deliberately absent from
# the list below; it is pinned for consistency, not built by this script.
set -uo pipefail
cd "$(dirname "$0")"
OUT=.olean-dev
export LEAN_PATH="$PWD/$OUT"
export LEAN_NUM_THREADS=4

# Module paths relative to LeanSemanticPrimitives/, in dependency order.
MODULES=(
  SemanticPrimitives
  Translation/LGraphModel
  Translation/GraphRefine
  Compiler/DesignCert
  Compiler/Runtime
  Compiler/DesignCertWF
  Compiler/DesignSemantics
  Projection/ObjectLanguage
  Projection/ObjectLanguageSemantics
  Projection/BindingTime
  Projection/Encoding
  Projection/Surface
  Projection/BTA
  Projection/PartialEvaluator
  Projection/MixProgram
  Projection/Demo
  Projection/Gate0
  Projection/PartialEvaluatorCorrect
  Projection/SecondProjection
  Projection/SimulatorContract
  Projection/Audit
)

# A bare name on the command line means a Projection module; a path is taken
# as given, so `./.build-proj.sh Compiler/DesignSemantics` also works.
if [ $# -gt 0 ]; then
  MODULES=()
  for a in "$@"; do
    if   [ -f "LeanSemanticPrimitives/$a.lean" ];            then MODULES+=("$a")
    elif [ -f "LeanSemanticPrimitives/Projection/$a.lean" ]; then MODULES+=("Projection/$a")
    else echo "  no such module: $a" >&2; exit 2
    fi
  done
fi

rc=0
for m in "${MODULES[@]}"; do
  src="LeanSemanticPrimitives/$m.lean"
  [ -f "$src" ] || continue
  olean="$OUT/LeanSemanticPrimitives/$m.olean"
  mkdir -p "$(dirname "$olean")"
  printf '  %-38s ' "$m"
  t0=$(date +%s%N)
  if out=$(taskset -c 0-3 nice -n 19 ionice -c3 lean -o "$olean" "$src" 2>&1); then
    t1=$(date +%s%N); printf 'ok   %5s ms\n' "$(( (t1-t0)/1000000 ))"
    [ -n "$out" ] && echo "$out" | sed 's/^/      /'
  else
    t1=$(date +%s%N); printf 'FAIL %5s ms\n' "$(( (t1-t0)/1000000 ))"
    echo "$out" | head -25 | sed 's/^/      /'
    rc=1
  fi
done
exit $rc
