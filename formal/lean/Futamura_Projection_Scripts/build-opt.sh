#!/usr/bin/env bash
# Build a probe at an EXPLICIT optimization level into a SEPARATE artifact and
# olean cache, so neither `.olean-dev` nor `.native-dev` -- which hold the
# binaries of in-flight experiments -- is touched.
#
#   OPT=-O2 ROOT=/abs/path bash Futamura_Projection_Scripts/build-opt.sh Futamura_Projection_Scripts/total_probe.lean [more.lean ...]
#   (the proof-tool drivers: ../../pass/lean/tools/drivers/sym_verify.lean sym_refmap.lean)
# OPT may carry several flags, e.g. OPT="-O2 -g -fno-omit-frame-pointer"
# for a profiling build that keeps -O2 but can be unwound.
#
# Incremental: a module is rebuilt only when its object is missing or older than
# its source, its .olean or its .c (an interrupted build), or when a module it
# imports (`lean --deps`) has a newer .olean than its own. Modules go in
# dependency order, so a rebuilt module rebuilds every module that imports it,
# directly or not. A probe is recompiled by the same rule and relinked when an
# object is newer than its binary. A different toolchain or OPT (the stamp
# file $ROOT/build-opt.stamp) or FORCE=1 rebuilds everything. The C compiles
# (leanc) run in the background, JOBS at once (default 4), and so do the
# probes; `lean` builds one module at a time.
set -uo pipefail
cd "$(dirname "$0")/.."
OPT=${OPT:--O2}
ROOT=${ROOT:?set ROOT to an absolute, project-local build directory}
OUT="$ROOT/olean"; NAT="$ROOT/nat"
mkdir -p "$OUT/LeanSemanticPrimitives/Projection/Proto" \
         "$OUT/HDLProof/Sym" "$OUT/HDLProof/Theory" "$OUT/HDLProof/Kit" "$OUT/HDLProof/Check" \
         "$OUT/LeanSemanticPrimitives/Compiler" \
         "$OUT/LeanSemanticPrimitives/Translation" "$NAT"
export LEAN_PATH="$OUT"

MODULES=(
  SemanticPrimitives Translation/LGraphModel Translation/FastBV Translation/GraphRefine
  Compiler/DesignCert Compiler/Runtime Compiler/DesignCertWF
  # master's Compiler/CertIO imports the Mathlib compiler chain, which this
  # route does not build; its DCERT2 CODEC is Compiler/CertIOCodec and the
  # format router Compiler/CertIORoute, both Mathlib-free. The probes load
  # through Projection/CertLoadAny: DCERT1 exactly as before (CertIOV1),
  # DCERT2 via readCertChecked. Dependency ORDER and the invalidation rule
  # below are unchanged; nothing is deleted.
  # MemDomains (decision #9: which clock domains of a memory fire) before the
  # step semantics that reads it.
  Compiler/MemDomains
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
  # the whole-array load and read (decision #2), before the nodes that admit them
  Projection/HardwareMemLoad
  Projection/HardwareMemTraversal Projection/HardwareMemShapes
  # the commit table's evaluation lemmas (decision #9), before the commit proofs
  Projection/HardwareMemNodes Projection/HardwareMemFold Projection/HardwareMemTable Projection/HardwareMemCommit
  Projection/HardwareMemOutFlop Projection/HardwareMemMain Projection/HardwareMemWidth
  Projection/HardwareMemAdmit Projection/HardwareMemSlotTy Projection/HardwareMemRuntime
  Projection/HardwareMemFixture Projection/HardwareMemWrite Projection/MemSupport
  Projection/EvalFuelArr Projection/MemProjection Projection/HardwareMemWriteAdmit
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
  # The IR/VC trace driver (cert_trace, and its route:vc): the direct simulator
  # and the verified compiler's executable half, all Mathlib-free.
  Compiler/DirectSemantics Compiler/ResidualIR Compiler/ResidualSemantics
  Compiler/CompileOpDefs Compiler/CompileGraphDefs Compiler/CompileDesignDefs
)
# The proof tools (pass/lean/tools, HDLProof): a second source root.
TOOLS="$(cd ../../pass/lean/tools && pwd)"
TOOL_MODULES=(
  # verified symbolic interpretation and k-induction (sym_verify, sym_refmap)
  HDLProof/Sym/Kit HDLProof/Sym/IntDiv HDLProof/Sym/Bridge HDLProof/Sym/Val HDLProof/Sym/Prim
  HDLProof/Sym/Mem HDLProof/Sym/EqV HDLProof/Sym/CtorP HDLProof/Sym/Dispatch HDLProof/Sym/Wire HDLProof/Sym/Eval HDLProof/Sym/Asg
  HDLProof/Sym/Sat HDLProof/Sym/Layout HDLProof/Sym/Step
  HDLProof/Sym/NativeOps HDLProof/Sym/NativeSound HDLProof/Sym/NativeBuilders HDLProof/Sym/Native HDLProof/Sym/Trace HDLProof/Sym/NativeInterp HDLProof/Sym/NativeRoute HDLProof/Kit/SymRoute
  HDLProof/Sym/Shape HDLProof/Sym/Unroll HDLProof/Sym/Oblig HDLProof/Sym/KInd HDLProof/Sym/Conj HDLProof/Sym/ConjT HDLProof/Sym/Closed
  HDLProof/Sym/BvLit
  # refinement maps (sym_refmap): theory, kit, checker, spec facts, composition
  HDLProof/Theory/Channels HDLProof/Theory/Stutter HDLProof/Theory/Compose HDLProof/Theory/Progress
  HDLProof/Kit/Norm HDLProof/Kit/BitE HDLProof/Check/RefMap HDLProof/Check/RefMapSound
  HDLProof/Check/SpecFacts HDLProof/Check/RefMapCompose HDLProof/Check/RefMapRef
  # the judge (sym_refmap judge): every stuttering alignment on one concrete run
  HDLProof/Kit/Judge
  # the drivers' shared IO helpers
  HDLProof/Kit/Driver
)
[ $# -ge 1 ] || { echo "usage: OPT=.. ROOT=.. build-opt.sh probe.lean [probe.lean ...]" >&2; exit 2; }
JOBS=${JOBS:-4}
STAMP="$ROOT/build-opt.stamp"
WANT="$(lean --version) | leanc $OPT"
FULL=0
if [ "${FORCE:-0}" = 1 ] || [ ! -f "$STAMP" ] || [ "$(cat "$STAMP")" != "$WANT" ]; then
  FULL=1; rm -f "$STAMP"
fi
FAILS="$NAT/.failed"; rm -rf "$FAILS"; mkdir -p "$FAILS"
T0=$SECONDS

# stale <src> <olean> <c> <obj>: true when the module (or probe) must be rebuilt
stale() {
  [ "$FULL" = 1 ] && return 0
  local src=$1 olean=$2 c=$3 obj=$4 d deps
  [ -f "$olean" ] && [ -f "$c" ] && [ -f "$obj" ] || return 0
  if [ "$src" -nt "$obj" ] || [ "$olean" -nt "$obj" ] || [ "$c" -nt "$obj" ]; then return 0; fi
  deps=$(lean --deps "$src" 2>/dev/null) || return 0
  while IFS= read -r d; do
    case "$d" in "$OUT"/*) [ "$d" -nt "$olean" ] && return 0 ;; esac
  done <<< "$deps"
  return 1
}
# slot: wait until fewer than JOBS background jobs run
slot() { while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do wait -n; done; }
# cc <name>: compile $NAT/<name>.c in the background (the object appears only when complete)
cc() {
  slot
  { leanc $OPT -c -o "$NAT/$1.o.tmp" "$NAT/$1.c" && mv -f "$NAT/$1.o.tmp" "$NAT/$1.o"; } \
    || { echo "leanc failed: $1" >&2; touch "$FAILS/$1"; } &
}
OBJS=(); REBUILT=0; KEPT=0
# module <src> <olean> <name> [lean flags]: build one module unless it is up to date
module() {
  local src=$1 olean=$2 flat=$3; shift 3
  OBJS+=("$NAT/$flat.o")
  if ! stale "$src" "$olean" "$NAT/$flat.c" "$NAT/$flat.o"; then KEPT=$((KEPT + 1)); return 0; fi
  local t=$SECONDS
  lean "$@" -o "$olean" -c "$NAT/$flat.c" "$src" || { wait; exit 1; }
  echo "  lean $flat: $((SECONDS - t)) s"
  REBUILT=$((REBUILT + 1))
  cc "$flat"
}
for m in "${MODULES[@]}"; do
  module "LeanSemanticPrimitives/$m.lean" "$OUT/LeanSemanticPrimitives/$m.olean" "${m//\//_}"
done
for m in "${TOOL_MODULES[@]}"; do
  module "$TOOLS/$m.lean" "$OUT/$m.olean" "${m//\//_}" -R "$TOOLS"
done
wait
[ -z "$(ls -A "$FAILS")" ] || { echo "C compile failed: $(ls "$FAILS" | tr '\n' ' ')" >&2; exit 1; }
echo "modules: $REBUILT rebuilt, $KEPT up to date ($((SECONDS - T0)) s)"
# One or more probes, all linked against the modules built above (so several
# drivers cost one module build, not one each), each in the background.
for PROBE in "$@"; do
  pb=$(basename "$PROBE" .lean)
  # the probe's own directory is its root, so a driver outside formal/lean
  # (pass/lean/tools/drivers) compiles too
  root="$(cd "$(dirname "$PROBE")" && pwd)"
  slot
  (
    if stale "$PROBE" "$NAT/$pb.olean" "$NAT/$pb.c" "$NAT/$pb.o"; then
      lean -R "$root" -o "$NAT/$pb.olean" -c "$NAT/$pb.c" "$PROBE" || exit 1
      leanc $OPT -c -o "$NAT/$pb.o.tmp" "$NAT/$pb.c" && mv -f "$NAT/$pb.o.tmp" "$NAT/$pb.o" || exit 1
    fi
    link=0
    [ -f "$NAT/$pb" ] || link=1
    for o in "$NAT/$pb.o" "${OBJS[@]}"; do [ "$o" -nt "$NAT/$pb" ] && { link=1; break; }; done
    if [ "$link" = 1 ]; then
      leanc $OPT -o "$NAT/$pb.tmp" "$NAT/$pb.o" "${OBJS[@]}" && mv -f "$NAT/$pb.tmp" "$NAT/$pb" || exit 1
      echo "built $NAT/$pb at $OPT"
    else
      echo "up to date: $NAT/$pb"
    fi
  ) || { echo "probe failed: $PROBE" >&2; touch "$FAILS/$pb"; } &
done
wait
[ -z "$(ls -A "$FAILS")" ] || { echo "failed: $(ls "$FAILS" | tr '\n' ' ')" >&2; exit 1; }
echo "$WANT" > "$STAMP"
echo "done in $((SECONDS - T0)) s"
