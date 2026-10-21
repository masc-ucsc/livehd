#!/usr/bin/env bash
# Build the shared semantic core, the Projection modules and the proof tools
# (pass/lean/tools, HDLProof) with plain `lean`
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
  # S3 (deferred shifting) and its proof; the @[csimp] lemma must be in scope
  # wherever mixDriver is compiled, so this precedes HardwareInterpreter.
  Projection/PartialEvaluatorS3
  Projection/S3Correct
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
  Projection/SetMaskLoop
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
  # The memory entry with `env0` inlined (the scalar route's TOTAL-variant
  # rewrite, applied to mainM): memVar_iff(W), projectMemVarDesignAt(W)_correct.
  Projection/Proto/InterpreterVariant
  Projection/Proto/VariantTransport
  Projection/Proto/RewriteTotal
  Projection/MemVariant
  Projection/MemVariantSim
  # ... and the node fold fused into its readers (MemFused): the default route
  Projection/MemFused
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

# The proof tools (HDLProof, pass/lean/tools): a second source root, compiled
# into the same olean dir after the simulator above, which they import.
TOOLS="$(cd ../../pass/lean/tools && pwd)"
TOOL_MODULES=(
  # refinement theory, pure Lean: channel specs and Kahn determinism, stuttering
  # runs, horizontal/vertical composition, progress under fairness
  HDLProof/Theory/Channels
  HDLProof/Theory/Stutter
  HDLProof/Theory/Compose
  HDLProof/Theory/Progress
  # the verified symbolic interpretation of a residual
  HDLProof/Sym/Kit
  HDLProof/Sym/Bridge
  HDLProof/Sym/Val
  HDLProof/Sym/Prim
  HDLProof/Sym/Mem
  HDLProof/Sym/Dispatch
  HDLProof/Sym/Wire
  HDLProof/Sym/Eval
  HDLProof/Sym/Asg
  HDLProof/Sym/Sat
  HDLProof/Sym/Layout
  HDLProof/Sym/Step
  HDLProof/Sym/Trace
  HDLProof/Sym/Shape
  HDLProof/Sym/Unroll
  HDLProof/Sym/Oblig
  HDLProof/Sym/KInd
  HDLProof/Sym/Conj
  HDLProof/Sym/ConjT
  HDLProof/Sym/Refine
  # the tools' audit: fails the build on a non-standard axiom
  HDLProof/Check/Audit
)

# A bare name on the command line means a Projection module; a path is taken
# as given, so `./.build-proj.sh Compiler/DesignSemantics` also works, and a
# tools module is named by its HDLProof path (`HDLProof/Sym/ConjT`).
if [ $# -gt 0 ]; then
  MODULES=(); TOOL_MODULES=()
  for a in "$@"; do
    if   [ -f "LeanSemanticPrimitives/$a.lean" ];            then MODULES+=("$a")
    elif [ -f "LeanSemanticPrimitives/Projection/$a.lean" ]; then MODULES+=("Projection/$a")
    elif [ -f "$TOOLS/$a.lean" ];                            then TOOL_MODULES+=("$a")
    else echo "  no such module: $a" >&2; exit 2
    fi
  done
fi

rc=0
# build_one <label> <root> <src> <olean>: the module name is the source path
# relative to <root>.
build_one() {
  mkdir -p "$(dirname "$4")"
  printf '  %-38s ' "$1"
  t0=$(date +%s%N)
  if out=$(taskset -c 0-3 nice -n 19 ionice -c3 lean -R "$2" -o "$4" "$3" 2>&1); then
    t1=$(date +%s%N); printf 'ok   %5s ms\n' "$(( (t1-t0)/1000000 ))"
    [ -n "$out" ] && echo "$out" | sed 's/^/      /'
  else
    t1=$(date +%s%N); printf 'FAIL %5s ms\n' "$(( (t1-t0)/1000000 ))"
    echo "$out" | head -25 | sed 's/^/      /'
    rc=1
  fi
}
for m in "${MODULES[@]}"; do
  src="LeanSemanticPrimitives/$m.lean"
  [ -f "$src" ] || continue
  build_one "$m" "$PWD" "$src" "$OUT/LeanSemanticPrimitives/$m.olean"
done
for m in "${TOOL_MODULES[@]}"; do
  [ -f "$TOOLS/$m.lean" ] || continue
  build_one "$m" "$TOOLS" "$TOOLS/$m.lean" "$OUT/$m.olean"
done
exit $rc
