#!/usr/bin/env bash
# PRIVATE, EXPLICIT build of the fourteen canonical Compiler targets the D3
# probes load -- the step that the default `lake build` does NOT cover.
#
# WHY THIS EXISTS.  `lake build` with no target builds the library root's import
# closure (7 jobs here).  The fourteen `Compiler/*` modules in `CRITICAL_OLEANS`
# are NOT in that closure, so a default build succeeding -- and a default
# `--no-build` reporting "All targets up-to-date" -- says nothing about them.
# Measured in the private checkout: the explicit 14-target `lake --no-build
# build` exited 3 at `Compiler.DesignCert`, and still exited 3 at
# `Compiler.ResidualIR` after an unrelated 34-job Projection build.  Hash
# equality with an audited artifact set is PREPARATION; it is not a validated
# source/build closure, and neither is default-target success.
#
# WHAT IT REFUSES.  A build root that is a symlink, a dirty worktree, a
# `D3_ARTIFACT_DIR` in the environment, and -- the one that bit us -- a build
# into a context that still has LIVE IMPORTERS.  Writing `.olean`s under a root
# another process is importing from is exactly the shared-mutable-state hazard
# this private root exists to avoid; equal hashes afterwards do not prove the
# window was harmless.
#
# COST ACCOUNTING.  Dependency preparation and build cost are recorded here and
# belong to NO generation, simulation or readiness metric.
#
#   private_compiler_build.sh <private-checkout> <out-dir>
set -uo pipefail
P=${1:?private checkout root}; OUT=${2:?out dir}
PABS=$(cd "$P" && pwd -P) || { echo "REFUSING: cannot resolve $P"; exit 2; }
L=$PABS/formal/lean

TARGETS=(CompileDesignDefs CompileGraphDefs CompileOpDefs CompileDesign CompileGraph
         CompileOp D3Harness ReifyGen Reify ReifyProof Runtime DesignCert
         ResidualIR ResidualSemantics)
[ "${#TARGETS[@]}" = 14 ] || { echo "REFUSING: target list is ${#TARGETS[@]}, expected 14"; exit 2; }
MODS=(); for t in "${TARGETS[@]}"; do MODS+=("LeanSemanticPrimitives.Compiler.$t"); done
ARTDIR=$L/.lake/build/lib/lean/LeanSemanticPrimitives/Compiler

[ -d "$L" ] || { echo "REFUSING: no formal/lean at $L"; exit 2; }
[ -L "$L/.lake" ] && { echo "REFUSING: $L/.lake is a symlink -- that is a shared build-root alias"; exit 2; }
[ -n "${D3_ARTIFACT_DIR:-}" ] && { echo "REFUSING: D3_ARTIFACT_DIR is set; that seam changes what is hashed without changing what is loaded"; exit 2; }
if [ -n "$(git -C "$PABS" status --porcelain)" ]; then
  echo "REFUSING: $PABS is dirty; a build whose source nobody can regenerate is not evidence"; exit 2
fi

# LIVE IMPORTERS.  Any other process whose command line references this private
# root may be reading the very artifacts this build would replace.
mine=$$
live=$(ps -eo pid,args --no-headers 2>/dev/null \
       | awk -v root="$PABS" -v me="$mine" '$1!=me && index($0,root)>0 {print}' \
       | grep -v "private_compiler_build.sh" | grep -v "[a]wk -v root")
if [ -n "$live" ]; then
  echo "REFUSING: this build context has live importers; wait for them to reach a terminal result"
  echo "$live" | cut -c1-140
  exit 4
fi

mkdir "$OUT" 2>/dev/null || { echo "REFUSING: attempt root $OUT already exists"; exit 3; }

# ---- identities BEFORE the build -----------------------------------------
{ echo "started_at=$(date -Iseconds)"
  echo "host=$(uname -n) kernel=$(uname -r) nproc=$(nproc)"
  echo "cwd=$L"
  echo "private_root=$PABS"
  echo "private_head=$(git -C "$PABS" rev-parse HEAD)"
  echo "private_status_clean=yes"
  echo "lean_toolchain=$(cat "$L/lean-toolchain")"
  echo "lake_version=$(cd "$L" && lake --version 2>&1 | head -1)"
  echo "lakefile_sha256=$(sha256sum "$L/lakefile.toml" | cut -d' ' -f1)"
  echo "lake_manifest_sha256=$(sha256sum "$L/lake-manifest.json" 2>/dev/null | cut -d' ' -f1)"
  echo "argv=lake build --no-cache ${MODS[*]}"
  for t in "${TARGETS[@]}"; do
    src=$L/LeanSemanticPrimitives/Compiler/$t.lean
    echo "source=$t sha256=$(sha256sum "$src" 2>/dev/null | cut -d' ' -f1) bytes=$(stat -c%s "$src" 2>/dev/null)"
  done
} > "$OUT/BINDING.txt"
for t in "${TARGETS[@]}"; do
  f=$ARTDIR/$t.olean
  if [ -f "$f" ]; then echo "$t $(sha256sum "$f" | cut -d' ' -f1) $(stat -c%s "$f")"
  else echo "$t <absent> 0"; fi
done > "$OUT/artifacts.before"

# ---- the build (dependency-preparation cost, not a generation metric) -----
( cd "$L" && /usr/bin/time -f 'wall %e\nuser %U\nsys %S\nmaxrss_kib %M\nexit %x' -o "$OUT/build.time" \
    nice -n19 ionice -c3 env LEAN_NUM_THREADS=4 lake build --no-cache "${MODS[@]}" ) \
  > "$OUT/build.stdout" 2> "$OUT/build.stderr"
rcb=$?; echo "$rcb" > "$OUT/build.exit"
tb=$(awk '$1=="exit"{print $2; f=1} END{exit !f}' "$OUT/build.time" 2>/dev/null); tbr=$?
if [ "$tbr" != 0 ] || [ -z "$tb" ]; then
  echo "MISSING build producer status (wrapper rc=$rcb)" | tee "$OUT/DISPOSITION.txt"; exit 97; fi
[ "$tb" = "$rcb" ] || { echo "DISAGREE build wrapper=$rcb producer=$tb" | tee "$OUT/DISPOSITION.txt"; exit 96; }

for t in "${TARGETS[@]}"; do
  f=$ARTDIR/$t.olean
  if [ -f "$f" ]; then echo "$t $(sha256sum "$f" | cut -d' ' -f1) $(stat -c%s "$f")"
  else echo "$t <absent> 0"; fi
done > "$OUT/artifacts.after"
# Both identities are kept.  A changed artifact needs fresh evidence only for
# what it affects; nothing that already passed is blindly re-run.
diff "$OUT/artifacts.before" "$OUT/artifacts.after" > "$OUT/artifacts.diff"
chg=$(grep -c '^>' "$OUT/artifacts.diff" 2>/dev/null || echo 0)
echo "build rc=$rcb; $chg of 14 artifact identities changed"

[ "$rcb" = 0 ] || { echo "REFUSED build exit=$rcb" | tee "$OUT/DISPOSITION.txt"; exit "$rcb"; }

# ---- the required explicit 14-target freshness check ----------------------
( cd "$L" && /usr/bin/time -f 'wall %e\nuser %U\nsys %S\nmaxrss_kib %M\nexit %x' -o "$OUT/nobuild.time" \
    nice -n19 lake --no-build build "${MODS[@]}" ) \
  > "$OUT/nobuild.stdout" 2> "$OUT/nobuild.stderr"
rcn=$?; echo "$rcn" > "$OUT/nobuild.exit"
if [ "$rcn" != 0 ]; then
  echo "NOT VALIDATED: the explicit 14-target --no-build exited $rcn, not 0" | tee "$OUT/DISPOSITION.txt"
  grep -m3 'out-of-date\|Building' "$OUT/nobuild.stdout" 2>/dev/null
  exit "$rcn"
fi
echo "VALIDATED: build exit 0 and the explicit 14-target --no-build exit 0 ($chg/14 artifacts changed)" \
  | tee "$OUT/DISPOSITION.txt"
exit 0
