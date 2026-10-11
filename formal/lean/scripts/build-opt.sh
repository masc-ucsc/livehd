#!/usr/bin/env bash
# Build a probe at an EXPLICIT optimization level into a SEPARATE artifact and
# olean cache, so neither `.olean-dev` nor `.native-dev` -- which hold the
# binaries of in-flight experiments -- is touched.
#
#   OPT=-O2 ROOT=/abs/path bash scripts/build-opt.sh scripts/total_probe.lean
# OPT may carry several flags, e.g. OPT="-O2 -g -fno-omit-frame-pointer"
# for a profiling build that keeps -O2 but can be unwound.
set -uo pipefail
cd "$(dirname "$0")/.."
OPT=${OPT:--O2}
ROOT=${ROOT:?set ROOT to an absolute, project-local build directory}
OUT="$ROOT/olean"; NAT="$ROOT/nat"
mkdir -p "$OUT/LeanSemanticPrimitives/Projection/Proto" \
         "$OUT/LeanSemanticPrimitives/Compiler" \
         "$OUT/LeanSemanticPrimitives/Translation" "$NAT"
export LEAN_PATH="$OUT"

MODULES=(
  SemanticPrimitives Translation/LGraphModel Translation/GraphRefine
  Compiler/DesignCert Compiler/Runtime Compiler/DesignCertWF
  Compiler/DesignSemantics Compiler/CertIO
  Projection/ObjectLanguage Projection/ObjectLanguageSemantics
  Projection/BindingTime Projection/Encoding Projection/Surface
  Projection/SurfaceSemantics Projection/BTA Projection/PartialEvaluator
  Projection/MixProgram Projection/PartialEvaluatorCorrect
  Projection/SecondProjection Projection/SimulatorContract
  Projection/DesignEncoding Projection/RuntimeEncoding Projection/OperatorBridge
  Projection/HardwareInterpreter Projection/HardwareAdequacy
  Projection/ProjectionCorrect Projection/ResidualFragment
  Projection/ProjectedStep Projection/CertLoad
  Projection/Proto/PartialEvaluatorFast Projection/Proto/PartialEvaluatorSummary
  Projection/Proto/PartialEvaluatorShift
  Projection/Proto/InterpreterVariant
  Projection/Proto/VariantTransport Projection/Proto/RewriteTotal
  Projection/Proto/VariantAdequacy Projection/Proto/VariantExec
  Projection/Proto/RunnerSupport
)
OBJS=()
for m in "${MODULES[@]}"; do
  flat=${m//\//_}
  lean -o "$OUT/LeanSemanticPrimitives/$m.olean" -c "$NAT/$flat.c" \
       "LeanSemanticPrimitives/$m.lean" || exit 1
  leanc $OPT -c -o "$NAT/$flat.o" "$NAT/$flat.c" || exit 1
  OBJS+=("$NAT/$flat.o")
done
PROBE=${1:?probe source}; pb=$(basename "$PROBE" .lean)
lean -o "$NAT/$pb.olean" -c "$NAT/$pb.c" "$PROBE" || exit 1
leanc $OPT -c -o "$NAT/$pb.o" "$NAT/$pb.c" || exit 1
leanc $OPT -o "$NAT/$pb" "$NAT/$pb.o" "${OBJS[@]}" || exit 1
echo "built $NAT/$pb at $OPT"
