#!/usr/bin/env bash
# Build the shared semantic core + the Projection modules with plain `lean`
# (no lake, no Mathlib).
#
# The shared core -- SemanticPrimitives, Translation/LGraphModel,
# Translation/GraphRefine and the Mathlib-free part of Compiler/* -- is
# master's own (the FP branch pinned a livehd-new revision; master carries the
# SHL/SRA/Sext boundary repairs, see Projection/SHARED_SEMANTICS.md).  None of
# it imports Mathlib, so it builds here in seconds alongside everything else.
#
# Mathlib enters only through Compiler/{CompileOp,CompileGraph,CompileDesign}
# and Translation/OpBridge; anything importing those (Compiler/CertIO's
# runChecked_correct, CertIORoundTrip, ...) belongs to `lake build`, not here.
# Every Projection module, including the whole memory route, is Mathlib-free
# and is built by this script.
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
  # Compiler/CertIO itself is NOT built here: its verified entry point
  # (runChecked_correct) imports Compiler/CompileDesign, whose chain imports
  # Mathlib, so it belongs to `lake build`. The DCERT2 CODEC it used to
  # contain is Compiler/CertIOCodec (Mathlib-free), and the format router is
  # Compiler/CertIORoute; both are built here, next to FP's DCERT1 codec.
  Compiler/CertIOV1
  Compiler/CertIOCodec
  Compiler/CertIORoute
  # Run admission (decisions C3, C4): exact edge vectors, reset-polarity guard.
  Compiler/ResetPolarity
  Compiler/RunAdmission
  # The axiom gate (`#audit_axioms`), first: every module that audits imports it.
  Projection/AxiomGate
  Projection/ObjectLanguage
  Projection/ObjectLanguageSemantics
  Projection/BindingTime
  Projection/Encoding
  Projection/Surface
  Projection/SurfaceSemantics
  Projection/BTA
  Projection/PartialEvaluator
  Projection/MixProgram
  Projection/Demo
  Projection/Gate0
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
  # DCERT1 or DCERT2: one loader for the FP runners (CertLoad stays DCERT1-only).
  Projection/CertLoadAny
  # The memory route (master-only): finite memory images, the hwMemS entry,
  # its adequacy chain, the static and runtime admission checks, BTA and the
  # proved specialization on the memory entry. Mathlib-free; Compiler/DirectCheck
  # (also Mathlib-free) supplies the checker's slot kinds.
  Projection/MemImage
  Projection/HardwareMem
  Projection/HardwareMemSource
  Projection/HardwareMemOp
  Projection/HardwareMemSlots
  Compiler/DirectCheck
  Projection/HardwareMemTraversal
  Projection/HardwareMemShapes
  Projection/HardwareMemNodes
  Projection/HardwareMemFold
  Projection/HardwareMemCommit
  Projection/HardwareMemOutFlop
  Projection/HardwareMemMain
  Projection/HardwareMemWidth
  Projection/HardwareMemAdmit
  Projection/HardwareMemChecks
  Projection/HardwareMemCycle
  Projection/HardwareMemSlotTy
  Projection/HardwareMemRuntime
  Projection/HardwareMemFixture
  Projection/HardwareMemWrite
  Projection/MemSupport
  Projection/MemProjection
  Projection/HardwareMemWriteAdmit
  Projection/MemLog
  Projection/MemPrimChecks
  # host vs object specializer on the three memory primitives
  Projection/MemMixMirrorCheck
  Projection/Proto/ShiftBridge
  # The bridge: exact edges + certWfError imply every route's edge premise.
  Projection/EdgePolicy
  # Residual serialization: a proved codec (decodeProgram_encodeProgram).
  Projection/ResidualIO
  Projection/Scaling
  # The central audit: `#audit_axioms` FAILS this build on any axiom outside
  # propext/Classical.choice/Quot.sound. The memory-route modules above audit
  # their own theorems in place with the same command.
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
