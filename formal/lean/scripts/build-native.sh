#!/usr/bin/env bash
# Build a NATIVE binary for one probe, to separate "the interpreter is slow /
# runs out of stack" from "the specializer is slow".
#
# Bounded on purpose: it compiles the modules the probe actually needs, not the
# whole library, and it is a DIAGNOSTIC -- nothing in the project depends on it.
set -uo pipefail
cd "$(dirname "$0")/.."
OUT=.olean-dev
NAT=.native-dev
export LEAN_PATH="$PWD/$OUT"
mkdir -p "$NAT"

MODULES=(
  SemanticPrimitives
  Translation/LGraphModel
  Translation/GraphRefine
  Compiler/DesignCert
  Compiler/Runtime
  Compiler/DesignCertWF
  Compiler/DesignSemantics
  Compiler/CertIO
  Projection/ObjectLanguage
  Projection/ObjectLanguageSemantics
  Projection/BindingTime
  Projection/Encoding
  Projection/Surface
  Projection/SurfaceSemantics
  Projection/BTA
  Projection/PartialEvaluator
  Projection/MixProgram
  Projection/PartialEvaluatorCorrect
  Projection/SecondProjection
  Projection/SimulatorContract
  Projection/DesignEncoding
  Projection/RuntimeEncoding
  Projection/OperatorBridge
  Projection/HardwareInterpreter
  Projection/HardwareAdequacy
  Projection/ProjectionCorrect
  Projection/ResidualFragment
  Projection/ProjectedStep
  Projection/CertLoad
  Projection/Proto/PartialEvaluatorFast
  Projection/Proto/InterpreterVariant
)

# Dependency-aware invalidation.  MODULES is in DEPENDENCY ORDER, so once any
# module is rebuilt every later one is stale too -- an .olean change upstream
# changes the code generated downstream.  Comparing only a module's own source
# mtime (the first version of this script) silently kept stale .o files and
# would have made a before/after measurement meaningless.
#
#   --clean   wipe $NAT first.  USE THIS FOR ANY BEFORE/AFTER NUMBER.
CLEAN=0
if [ "${1:-}" = "--clean" ]; then CLEAN=1; shift; fi
if [ $CLEAN -eq 1 ]; then echo "  (clean: removing $NAT)"; rm -rf "$NAT"; mkdir -p "$NAT"; fi

DIRTY=0
OBJS=()
for m in "${MODULES[@]}"; do
  flat=${m//\//_}
  src=LeanSemanticPrimitives/$m.lean
  c=$NAT/$flat.c
  o=$NAT/$flat.o
  if [ $DIRTY -eq 1 ] || [ ! -f "$o" ] || [ "$src" -nt "$o" ]; then
    DIRTY=1
    printf '  %-44s ' "$m"
    s=$(date +%s%N)
    lean -o "$OUT/LeanSemanticPrimitives/$m.olean" -c "$c" "$src" || exit 1
    leanc -c -o "$o" "$c" || exit 1
    e=$(date +%s%N)
    echo "$(( (e-s)/1000000 )) ms"
  fi
  OBJS+=("$o")
done

# the probe is always rebuilt: it is cheap and it is what the numbers come from
PROBE=${1:-scripts/perf_probe.lean}
pb=$(basename "$PROBE" .lean)
printf '  %-44s ' "$pb (probe)"
s=$(date +%s%N)
lean -o "$NAT/$pb.olean" -c "$NAT/$pb.c" "$PROBE" || exit 1
leanc -c -o "$NAT/$pb.o" "$NAT/$pb.c" || exit 1
leanc -o "$NAT/$pb" "$NAT/$pb.o" "${OBJS[@]}" || exit 1
e=$(date +%s%N)
echo "$(( (e-s)/1000000 )) ms"
echo "built $NAT/$pb"
