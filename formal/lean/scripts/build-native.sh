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
)

OBJS=()
for m in "${MODULES[@]}"; do
  flat=${m//\//_}
  src=LeanSemanticPrimitives/$m.lean
  c=$NAT/$flat.c
  o=$NAT/$flat.o
  if [ ! -f "$o" ] || [ "$src" -nt "$o" ]; then
    printf '  %-44s ' "$m"
    s=$(date +%s%N)
    lean -o "$OUT/LeanSemanticPrimitives/$m.olean" -c "$c" "$src" || exit 1
    leanc -c -o "$o" "$c" || exit 1
    e=$(date +%s%N)
    echo "$(( (e-s)/1000000 )) ms"
  fi
  OBJS+=("$o")
done

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
