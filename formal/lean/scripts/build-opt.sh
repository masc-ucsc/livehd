#!/usr/bin/env bash
# Build a probe at an EXPLICIT optimization level into a SEPARATE artifact and
# olean cache, so neither `.olean-dev` nor `.native-dev` -- which hold the
# binaries of in-flight experiments -- is touched.
#
#   OPT=-O2 ROOT=/abs/path bash scripts/build-opt.sh scripts/total_probe.lean [more.lean ...]
# OPT may carry several flags, e.g. OPT="-O2 -g -fno-omit-frame-pointer"
# for a profiling build that keeps -O2 but can be unwound.
set -uo pipefail
cd "$(dirname "$0")/.."
OPT=${OPT:--O2}
ROOT=${ROOT:?set ROOT to an absolute, project-local build directory}
OUT="$ROOT/olean"; NAT="$ROOT/nat"
mkdir -p "$OUT/LeanSemanticPrimitives/Projection/Proto" \
         "$OUT/LeanSemanticPrimitives/Projection/Sym" \
         "$OUT/LeanSemanticPrimitives/Compiler" \
         "$OUT/LeanSemanticPrimitives/Translation" "$NAT"
export LEAN_PATH="$OUT"

MODULES=(
  SemanticPrimitives Translation/LGraphModel Translation/GraphRefine
  Compiler/DesignCert Compiler/Runtime Compiler/DesignCertWF
  # master's Compiler/CertIO imports the Mathlib compiler chain, which this
  # route does not build; its DCERT2 CODEC is Compiler/CertIOCodec and the
  # format router Compiler/CertIORoute, both Mathlib-free. The probes load
  # through Projection/CertLoadAny: DCERT1 exactly as before (CertIOV1),
  # DCERT2 via readCertChecked. Dependency ORDER and the invalidation rule
  # below are unchanged; nothing is deleted.
  Compiler/DesignSemantics Compiler/CertIOV1 Compiler/CertIOCodec Compiler/CertIORoute
  Compiler/ResetPolarity Compiler/RunAdmission
  Projection/ObjectLanguage Projection/ObjectLanguageSemantics
  Projection/BindingTime Projection/Encoding Projection/Surface
  Projection/SurfaceSemantics Projection/BTA Projection/PartialEvaluator
  Projection/PartialEvaluatorS3 Projection/S3Correct
  Projection/MixProgram Projection/PartialEvaluatorCorrect
  Projection/SecondProjection Projection/SimulatorContract
  Projection/DesignEncoding Projection/RuntimeEncoding Projection/OperatorBridge
  Projection/HardwareInterpreter Projection/SetMaskLoop Projection/HardwareAdequacy
  Projection/ProjectionCorrect Projection/ResidualFragment
  Projection/ProjectedStep Projection/CertLoad Projection/CertLoadAny
  # The memory route, for the DCERT2 memory-route driver (dcert2_memroute):
  # the axiom gate first (the memory modules audit their theorems with it),
  # then the chain in dependency order, Compiler/DirectCheck before the
  # traversal that uses it.
  Projection/AxiomGate
  Projection/MemImage Projection/HardwareMem Projection/HardwareMemSource
  Projection/HardwareMemOp Projection/HardwareMemSlots Compiler/DirectCheck
  Projection/HardwareMemTraversal Projection/HardwareMemShapes
  Projection/HardwareMemNodes Projection/HardwareMemFold Projection/HardwareMemCommit
  Projection/HardwareMemOutFlop Projection/HardwareMemMain Projection/HardwareMemWidth
  Projection/HardwareMemAdmit Projection/HardwareMemSlotTy Projection/HardwareMemRuntime
  Projection/HardwareMemFixture Projection/HardwareMemWrite Projection/MemSupport
  Projection/MemProjection Projection/HardwareMemWriteAdmit
  # residual serialization (gen_probe_v2 save:, gen_readiness_v2 load:)
  Projection/ResidualIO
  Projection/Proto/PartialEvaluatorFast Projection/Proto/PartialEvaluatorSummary
  Projection/Proto/PartialEvaluatorShift
  Projection/Proto/InterpreterVariant
  Projection/Proto/VariantTransport Projection/Proto/RewriteTotal
  Projection/Proto/VariantAdequacy Projection/Proto/VariantExec
  Projection/Proto/RunnerSupport
  # the memory entry with env0 inlined (projectMemVarDesignAt)
  Projection/MemVariant Projection/MemVariantSim Projection/MemFused
  # M3: verified symbolic interpretation and k-induction (sym_verify)
  Projection/Sym/Kit Projection/Sym/Bridge Projection/Sym/Val Projection/Sym/Prim
  Projection/Sym/Mem Projection/Sym/Dispatch Projection/Sym/Eval Projection/Sym/Asg
  Projection/Sym/Sat Projection/Sym/Layout Projection/Sym/Step Projection/Sym/Trace
  Projection/Sym/Shape Projection/Sym/Unroll Projection/Sym/Oblig Projection/Sym/KInd
  Projection/Sym/Refine
)
OBJS=()
for m in "${MODULES[@]}"; do
  flat=${m//\//_}
  lean -o "$OUT/LeanSemanticPrimitives/$m.olean" -c "$NAT/$flat.c" \
       "LeanSemanticPrimitives/$m.lean" || exit 1
  leanc $OPT -c -o "$NAT/$flat.o" "$NAT/$flat.c" || exit 1
  OBJS+=("$NAT/$flat.o")
done
# One or more probes, all linked against the modules built above (so several
# drivers cost one module build, not one each).
[ $# -ge 1 ] || { echo "usage: OPT=.. ROOT=.. build-opt.sh probe.lean [probe.lean ...]" >&2; exit 2; }
for PROBE in "$@"; do
  pb=$(basename "$PROBE" .lean)
  lean -o "$NAT/$pb.olean" -c "$NAT/$pb.c" "$PROBE" || exit 1
  leanc $OPT -c -o "$NAT/$pb.o" "$NAT/$pb.c" || exit 1
  leanc $OPT -o "$NAT/$pb" "$NAT/$pb.o" "${OBJS[@]}" || exit 1
  echo "built $NAT/$pb at $OPT"
done
