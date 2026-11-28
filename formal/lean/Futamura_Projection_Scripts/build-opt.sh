#!/usr/bin/env bash
# Build a probe at an EXPLICIT optimization level into a SEPARATE artifact and
# olean cache, so neither `.olean-dev` nor `.native-dev` -- which hold the
# binaries of in-flight experiments -- is touched.
#
#   OPT=-O2 ROOT=/abs/path bash Futamura_Projection_Scripts/build-opt.sh Futamura_Projection_Scripts/total_probe.lean [more.lean ...]
#   (the proof tools: ../../pass/lean/tools/proof/sym_verify.lean sym_refmap.lean ...;
#    the simulation drivers: ../../pass/lean/tools/sim/cert_run.lean lhd_ckpt.lean)
# OPT may carry several flags, e.g. OPT="-O2 -g -fno-omit-frame-pointer"
# for a profiling build that keeps -O2 but can be unwound.
# EXTRA_MODULES="Compiler/ReifyGen" builds further LeanSemanticPrimitives modules after the
# list below and links them into THIS invocation's probes only (a per-design TV driver,
# pass/lean/tools/scripts/make_tv_sim.py: ReifyGen imports Lean, whose front end such a
# binary then carries). Unset, nothing changes.
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
         "$OUT/HDLProof/Run" "$OUT/HDLProof/Reach" \
         "$OUT/LeanSemanticPrimitives/Compiler" \
         "$OUT/LeanSemanticPrimitives/Translation" "$NAT"
export LEAN_PATH="$OUT"

# The modules are the import closure of the roots below and of the probes on the command line,
# in dependency order (lean_modules.py, next to this script): a module that a root or a probe
# imports, however indirectly, is built and linked; a new module needs adding here only when
# nothing imports it. The list fails the build when an imported file is missing or anything
# imports Mathlib.
TOOLS="$(cd ../../pass/lean/tools && pwd)"
ROOTS=(
  # vc (the chain below ResidualWork) and the fp memory route's variants and runner support
  LeanSemanticPrimitives.Compiler.ResidualWork
  # the routes as engines (a route's step on a state of its own kind) and their soundness
  LeanSemanticPrimitives.Compiler.Engine
  LeanSemanticPrimitives.Compiler.VcEngine
  LeanSemanticPrimitives.Projection.MemVariantSim
  LeanSemanticPrimitives.Projection.ResidualIO
  LeanSemanticPrimitives.Projection.MixProgram
  LeanSemanticPrimitives.Projection.SecondProjection
  LeanSemanticPrimitives.Projection.Proto.RunnerSupport
  LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorFast
  LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorSummary
  LeanSemanticPrimitives.Projection.Proto.PartialEvaluatorShift
  # the proof tools: symbolic routes, refinement maps, the judge, the drivers' helpers,
  # reachability, cert_run
  HDLProof.Kit.SymRoute
  HDLProof.Sym.Diag
  HDLProof.Check.RefMapRef
  HDLProof.Kit.Judge
  HDLProof.Kit.Driver
  HDLProof.Reach.Refute
  HDLProof.Run.Sound
  HDLProof.Run.Vcd
  HDLProof.Run.Engine
  # cert_trace's open-loop trace for a step given as a function (per-design TV drivers)
  HDLProof.Run.Trace
)
[ $# -ge 1 ] || { echo "usage: OPT=.. ROOT=.. build-opt.sh probe.lean [probe.lean ...]" >&2; exit 2; }
# EXTRA_MODULES (paths under LeanSemanticPrimitives/) are roots of this invocation only, so they
# and whatever they import are built and linked into these probes
EXTRA_ROOTS=()
for m in ${EXTRA_MODULES:-}; do EXTRA_ROOTS+=("LeanSemanticPrimitives.${m//\//.}"); done
GEN=$(python3 Futamura_Projection_Scripts/lean_modules.py "${ROOTS[@]}" "${EXTRA_ROOTS[@]}" "$@") \
  || { echo "the module list could not be made (above)" >&2; exit 1; }
MODULES=(); TOOL_MODULES=()
while IFS= read -r m; do
  case "$m" in
    LeanSemanticPrimitives/*) MODULES+=("${m#LeanSemanticPrimitives/}") ;;
    HDLProof/*) TOOL_MODULES+=("$m") ;;
  esac
done <<< "$GEN"
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
  mkdir -p "$(dirname "$olean")"
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
  # (pass/lean/tools/proof, pass/lean/tools/sim) compiles too
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
# The sources each probe was built from (every module linked into it, and its own source), one
# `sha256sum` line each, paths from the repository root: $NAT/<probe>.sources.sha256, so a test
# can check that a native runs a build of the current sources (pass/lean/tools/tests/check_fresh.sh).
# Per probe: a later build-opt.sh for other probes relinks only those.
srcs=()
for m in "${MODULES[@]}"; do srcs+=("formal/lean/LeanSemanticPrimitives/$m.lean"); done
for m in "${TOOL_MODULES[@]}"; do srcs+=("pass/lean/tools/$m.lean"); done
for PROBE in "$@"; do
  pb=$(basename "$PROBE" .lean)
  rel=$(realpath --relative-to=../.. "$PROBE")
  (cd ../.. && sha256sum "${srcs[@]}" "$rel") > "$NAT/$pb.sources.sha256.tmp" \
    && mv -f "$NAT/$pb.sources.sha256.tmp" "$NAT/$pb.sources.sha256" \
    || { echo "could not hash the sources of $pb" >&2; rm -f "$NAT/$pb.sources.sha256"*; exit 1; }
done
echo "done in $((SECONDS - T0)) s"
