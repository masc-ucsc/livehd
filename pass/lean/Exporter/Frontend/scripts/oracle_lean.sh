# oracle_lean.sh: the Lean environment of the RTL oracles in this folder (*_oracle.sh that run
# the Lean drivers). Sourced, never run. One place, so that a module moving between the two builds
# below is fixed here and not in every script.
#
# Where each module the oracles load is built:
#   .olean-dev  cert_trace and dcert2_memroute (Futamura_Projection_Scripts) import the proof tools
#               (HDLProof, in pass/lean/tools) and the Mathlib-free core. Only
#               formal/lean/.build-proj.sh builds them, into formal/lean/.olean-dev; lake's
#               LEAN_PATH has no HDLProof, so a driver run on lake's path alone fails to load.
#   lake        the legacy models' proof bridges (Translation/OpBridge and the *Bridge modules)
#               import Mathlib, so only lake builds them, into formal/lean/.lake/build. Translation
#               validation (d3_sweep.py) runs under lake's own environment and loads lake-built
#               Compiler modules (ReifyGen, D3Harness, ReifyProof).
# Lean takes a package's root to be the FIRST LEAN_PATH entry that holds its directory
# (`LeanSemanticPrimitives/`) and reads every module of that package from there, so listing
# .olean-dev before lake's path hides every lake-built LeanSemanticPrimitives module ("object file
# .olean-dev/.../OpBridge.olean does not exist"). So LP's first entry is a merged copy made of
# links: HDLProof from .olean-dev, and each LeanSemanticPrimitives module from .olean-dev when it
# has the module, else from lake. Lake's path follows it, for Mathlib and the other packages.
#
#   oracle_lean <dir> [legacy] [d3]
#     Fails unless .olean-dev holds the drivers' imports. For each named step, lake-builds the
#     modules it loads (incremental: a no-op when they are current; log <dir>/lake_<step>.log):
#       legacy  the bridges a legacy model with `formal.lean.emit_fast_bridge=true` imports
#       d3      the Compiler modules d3_sweep.py's probes and proofs import
#     Then writes the merged copy to <dir>/lean_path and sets LP.
# LAKE names the lake binary (default lake). Every step runs in formal/lean, where the Lean pin is.

ORACLE_LEAN_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../../formal/lean" && pwd)"
ORACLE_LAKE_LEGACY=(LeanSemanticPrimitives.Translation.OpBridge LeanSemanticPrimitives.Translation.NaryBridge
                    LeanSemanticPrimitives.Translation.OrBridge LeanSemanticPrimitives.Translation.MulBridge
                    LeanSemanticPrimitives.Translation.SextBridge LeanSemanticPrimitives.Translation.WholeArrayBridge)
ORACLE_LAKE_D3=(LeanSemanticPrimitives.Compiler.ReifyGen LeanSemanticPrimitives.Compiler.D3Harness
                LeanSemanticPrimitives.Compiler.ReifyProof)

oracle_lean() { # <dir> [legacy] [d3]
  local dir=$1 step lp m
  shift
  local dev="$ORACLE_LEAN_DIR/.olean-dev"
  for m in HDLProof/Run/Trace LeanSemanticPrimitives/Projection/CertLoadAny; do
    [[ -s "$dev/$m.olean" ]] || {
      echo "FAIL: $dev has no $m.olean; build it first: (cd formal/lean && bash .build-proj.sh)"; exit 2; }
  done
  local mods=()
  for step in "$@"; do
    case $step in
      legacy) mods=("${ORACLE_LAKE_LEGACY[@]}") ;;
      d3) mods=("${ORACLE_LAKE_D3[@]}") ;;
      *) echo "oracle_lean: unknown step $step" >&2; exit 2 ;;
    esac
    (cd "$ORACLE_LEAN_DIR" && "${LAKE:-lake}" build "${mods[@]}") > "$dir/lake_$step.log" 2>&1 \
      || { tail -5 "$dir/lake_$step.log"; echo "FAIL: lake build for the $step step (log $dir/lake_$step.log)"; exit 1; }
  done
  python3 -I - "$dev" "$ORACLE_LEAN_DIR/.lake/build/lib/lean" "$dir/lean_path" <<'PY' \
    || { echo "FAIL: the merged LEAN_PATH entry $dir/lean_path"; exit 1; }
import os, pathlib, shutil, sys
dev, lake, out = (pathlib.Path(a) for a in sys.argv[1:4])
shutil.rmtree(out, ignore_errors=True)
out.mkdir(parents=True)
os.symlink(dev / "HDLProof", out / "HDLProof")
# A module's files share its stem (`OpBridge.olean`, `OpBridge.ilean`, ...): take them all from one
# build, .olean-dev first.
taken = set()
for src in (dev, lake):
    pkg = src / "LeanSemanticPrimitives"
    if not pkg.is_dir():
        continue
    mine = set()
    for f in sorted(pkg.rglob("*")):
        if not f.is_file():
            continue
        rel = f.relative_to(src)
        stem = rel.parent / rel.name.split(".", 1)[0]
        if stem in taken:
            continue
        mine.add(stem)
        (out / rel).parent.mkdir(parents=True, exist_ok=True)
        os.symlink(f, out / rel)
    taken |= mine
PY
  lp=$(cd "$ORACLE_LEAN_DIR" && "${LAKE:-lake}" env printenv LEAN_PATH) || { echo "FAIL: lake env printenv LEAN_PATH"; exit 1; }
  LP="$dir/lean_path:$lp"
}
