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

# The modules to build are the import closure of the roots below, in dependency order
# (Futamura_Projection_Scripts/lean_modules.py): a module that any root imports, however
# indirectly, is built, and a new module needs adding here only when no root imports it. The
# list fails the build when an imported file is missing or anything imports Mathlib.
# Mathlib-free modules only (see the header). The tools (pass/lean/tools, HDLProof) are a second
# source root, compiled into the same olean dir after the semantic core, which they import.
TOOLS="$(cd ../../pass/lean/tools && pwd)"
ROOTS=(
  # vc: the verified compiler, its proved optimizer and its incremental, event-driven, memory
  # table and chunk-skipping steps (the chain below ResidualWork)
  LeanSemanticPrimitives.Compiler.ResidualWork
  # the routes as engines (a route's step on a state of its own kind) and their soundness
  LeanSemanticPrimitives.Compiler.Engine
  LeanSemanticPrimitives.Compiler.VcEngine
  # the certificate semantics stated as plain rules: every operator at every width as core
  # BitVec's operation (OpSpec), and the clock, reset, enable and memory rules (StateSpec),
  # each theorem with its axiom-gate line
  LeanSemanticPrimitives.Translation.OpSpec
  LeanSemanticPrimitives.Compiler.StateSpec
  # the direct simulator (lgraph-sim's library) and its examples; DirectTests' 104 #guards run
  # while it builds (the accepted examples step and agree with interpretDesign; each malformed
  # certificate is refused for its stated reason; the async-reset and memory rules give the
  # contract's values), so a change that breaks them fails the build. Nothing else built them:
  # lake builds them only with lgraph-sim (`lake build LgraphSim`), which nothing runs.
  LeanSemanticPrimitives.Compiler.DirectSim
  LeanSemanticPrimitives.Compiler.DirectTests
  # translation validation's reifier (a meta-program; the per-design TV drivers use it)
  LeanSemanticPrimitives.Compiler.ReifyGen
  # the legacy models' fast operators (the per-design legacy drivers import it; interpreted runs
  # read it from .olean-dev)
  LeanSemanticPrimitives.Translation.LegacyFast
  # fp: the memory route's variants, residual IO and the drivers' shared runner support
  LeanSemanticPrimitives.Projection.MemVariantSim
  LeanSemanticPrimitives.Projection.ResidualIO
  LeanSemanticPrimitives.Projection.Proto.RunnerSupport
  LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorFast
  LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorSummary
  LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorShift
  LeanSemanticPrimitives.Projection.Proto.ShiftBridge
  # fp's checks, audits and measurements
  LeanSemanticPrimitives.Projection.Gate0
  LeanSemanticPrimitives.Projection.HardwareMemChecks
  LeanSemanticPrimitives.Projection.HardwareMemCycle
  LeanSemanticPrimitives.Projection.MemLog
  LeanSemanticPrimitives.Projection.MemPrimChecks
  LeanSemanticPrimitives.Projection.MemMixMirrorCheck
  LeanSemanticPrimitives.Projection.EdgePolicy
  LeanSemanticPrimitives.Projection.Scaling
  LeanSemanticPrimitives.Projection.Audit
  # the proof tools: symbolic routes, the judge, the drivers' helpers, reachability, cert_run
  HDLProof.Kit.SymRoute
  HDLProof.Sym.Diag
  HDLProof.Kit.Judge
  HDLProof.Kit.Driver
  HDLProof.Reach.Refute
  HDLProof.Run.Sound
  HDLProof.Run.Vcd
  HDLProof.Run.Engine
  # cert_trace's open-loop trace for a step given as a function (per-design TV drivers)
  HDLProof.Run.Trace
  # the tools' audit: fails the build on a non-standard axiom
  HDLProof.Check.Audit
)
# The proof tools (pass/lean/tools/proof), the simulation drivers (pass/lean/tools/sim) and the
# tools' tests run interpreted against .olean-dev: their imports are roots.
ROOT_FILES=(../../pass/lean/tools/proof/*.lean ../../pass/lean/tools/sim/*.lean Futamura_Projection_Scripts/cert_trace.lean
  Futamura_Projection_Scripts/dcert2_memroute.lean ../../pass/lean/tools/tests/*.lean)
GEN=$(python3 Futamura_Projection_Scripts/lean_modules.py "${ROOTS[@]}" "${ROOT_FILES[@]}") \
  || { echo "the module list could not be made (above)" >&2; exit 1; }
MODULES=(); TOOL_MODULES=()
while IFS= read -r m; do
  case "$m" in
    LeanSemanticPrimitives/*) MODULES+=("${m#LeanSemanticPrimitives/}") ;;
    HDLProof/*) TOOL_MODULES+=("$m") ;;
  esac
done <<< "$GEN"

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
  build_one "$m" "$PWD" "$src" "$OUT/LeanSemanticPrimitives/$m.olean"
done
for m in "${TOOL_MODULES[@]}"; do
  build_one "$m" "$TOOLS" "$TOOLS/$m.lean" "$OUT/$m.olean"
done
# The sources this .olean-dev was built from, one `sha256sum` line each (paths from the repository
# root), so a test can check that it runs on a build of the current sources
# (pass/lean/tools/tests/check_fresh.sh). A full build writes the list; a build of named modules
# updates their lines; a failed build removes it, since nothing then vouches for the .olean files.
SRCS="$OUT/SOURCES.sha256"
built=()
for m in "${MODULES[@]}"; do built+=("formal/lean/LeanSemanticPrimitives/$m.lean"); done
for m in "${TOOL_MODULES[@]}"; do built+=("pass/lean/tools/$m.lean"); done
if [ $rc -ne 0 ]; then
  rm -f "$SRCS"
elif [ ${#built[@]} -gt 0 ]; then
  new=$(cd ../.. && sha256sum "${built[@]}") || { rm -f "$SRCS"; exit 1; }
  if [ $# -eq 0 ]; then
    echo "$new" > "$SRCS"
  elif [ -f "$SRCS" ]; then
    { awk 'NR == FNR { skip[$0] = 1; next } !(substr($0, 67) in skip)' <(printf '%s\n' "${built[@]}") "$SRCS"
      echo "$new"; } > "$SRCS.tmp" && mv -f "$SRCS.tmp" "$SRCS"
  fi
fi
exit $rc
