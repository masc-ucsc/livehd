#!/usr/bin/env bash
# Build the CVA6 foundation IN THIS WORKTREE: filelist, elaborated AST, gate
# wrappers, and a manifest that pins every input by hash.
#
# WHY THIS EXISTS.  The old route (`run_cva6_vc_sweep.sh`) hardcodes
# `R=/HOMEROOT/projects/livehd-new` and reads that checkout's frozen
# filelists and its UNTRACKED `temp/wrap_all`.  Emitting `alu` from those
# wrappers gives 429 sources / 436 nodes against a corpus certificate of
# 6,137 / 6,597 -- a 5x discrepancy that looks like a result and is really a
# stale generation.  Nothing here reads another checkout.
#
# TWO FAIL-CLOSED PROPERTIES, both of which an earlier version lacked:
#
#   1. THE PUBLISHED CORPUS IS WHOLLY FRESH.  Generation goes to a staging
#      directory and is published only on success, and a module's status comes
#      from THIS invocation's explicit GEN/SKIP line -- never from "a file with
#      that name exists".  A planted `<mod>_gate.sv` left over from an earlier
#      run was previously reported GENERATED even though the generator said SKIP.
#   2. THE SOURCE STATE IS NAMED AND CLEAN.  CVA6_ROOT must be a git checkout
#      with NO tracked modifications, and the manifest records the revision, the
#      canonical path and the clean-state check, so a sweep can verify it is
#      looking at the same source state rather than merely at the same path.
#
# Usage:
#   scripts/build_cva6_foundation.sh [module-list]
#     module-list   default pass/lean/Lgraph_IR_Semantics_Simulator/tests/d4/cva6_78.list
#
# Env:
#   CVA6_ROOT       CVA6 checkout (default /HOMEROOT/projects/cva6-blockfinding,
#                   the revision the tracked module inventory was derived from)
#   CVA6_TARGET     bender/config target (default cv64a6_imafdc_sv39_hpdcache_wb)
#   SLANG           slang binary (default /HOMEROOT/projects/slang/build/bin/slang)
#   BENDER          bender binary (default: PATH)
#   FOUNDATION_OUT  output root (default <worktree>/generated).  Controls build
#                   into a scratch root so they never disturb the real corpus.
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
die() { printf 'FATAL: %s\n' "$*" >&2; exit 2; }

LIST="${1:-$ROOT/pass/lean/Lgraph_IR_Semantics_Simulator/tests/d4/cva6_78.list}"
CVA6_ROOT="${CVA6_ROOT:-/HOMEROOT/projects/cva6-blockfinding}"
CVA6_TARGET="${CVA6_TARGET:-cv64a6_imafdc_sv39_hpdcache_wb}"
SLANG="${SLANG:-/HOMEROOT/projects/slang/build/bin/slang}"
BENDER="${BENDER:-$(command -v bender 2>/dev/null || true)}"
OUTBASE="${FOUNDATION_OUT:-$ROOT/generated}"

[[ -s "$LIST" ]] || die "module list $LIST is missing or empty"
[[ -d "$CVA6_ROOT" ]] || die "CVA6_ROOT $CVA6_ROOT is not a directory"

# ---------------------------------------------------------------------------
# ONE NAMED, CLEAN SOURCE STATE.
# A revision alone is not the source state: a dirty tree at revision R is not R.
# Untracked files are allowed (the inventory `cva6_blocks.txt` is one, and it is
# not an input to the build); TRACKED modifications are not.
# ---------------------------------------------------------------------------
CVA6_REV="$(git -C "$CVA6_ROOT" rev-parse HEAD 2>/dev/null || true)"
[[ -n "$CVA6_REV" ]] || die "CVA6_ROOT $CVA6_ROOT is not a git checkout; the source revision must be nameable"
CVA6_DIRTY="$(git -C "$CVA6_ROOT" status --porcelain --untracked-files=no 2>/dev/null)"
[[ -z "$CVA6_DIRTY" ]] || die "CVA6_ROOT $CVA6_ROOT has TRACKED modifications; a dirty tree at $CVA6_REV is not $CVA6_REV:
$(sed 's/^/       /' <<<"$CVA6_DIRTY" | head -10)"
CVA6_ROOT_CANON="$(readlink -f "$CVA6_ROOT")"

CFG="$CVA6_ROOT/core/include/${CVA6_TARGET}_config_pkg.sv"
[[ -r "$CFG" ]] || die "no config package for target $CVA6_TARGET at $CFG"
[[ -x "$SLANG" ]] || die "no slang at $SLANG"
[[ -n "$BENDER" && -x "$BENDER" ]] || die "no bender on PATH; set BENDER=<path>"
GEN="$ROOT/scripts/gen_cva6_wrappers.py"
[[ -r "$GEN" ]] || die "missing $GEN"

mapfile -t MODULES < <(grep -v '^[[:space:]]*#' "$LIST" | grep -v '^[[:space:]]*$' | sed 's/[[:space:]]*$//')
N_DECLARED="${#MODULES[@]}"
[[ "$N_DECLARED" -gt 0 ]] || die "module list has no entries"
[[ "$N_DECLARED" -eq "$(printf '%s\n' "${MODULES[@]}" | sort -u | wc -l)" ]] || die "module list has duplicates"

FLDIR="$OUTBASE/cva6_filelists"; mkdir -p "$FLDIR"   || die "cannot create $FLDIR"
ASTDIR="$OUTBASE/cva6_ast";      mkdir -p "$ASTDIR"  || die "cannot create $ASTDIR"
WRAPDIR="$OUTBASE/cva6_wrappers"
MANDIR="$OUTBASE/cva6_foundation"; mkdir -p "$MANDIR" || die "cannot create $MANDIR"
FL="$FLDIR/${CVA6_TARGET}.flistplus.f"
AST="$ASTDIR/cva6_ast.json"
MAN="$MANDIR/provenance.txt"
WTSV="$MANDIR/wrappers.tsv"

STAGE="$WRAPDIR.staging.$$"
PREV="$WRAPDIR.prev.$$"
cleanup() { rm -rf "$STAGE"; [[ -d "$PREV" ]] && mv "$PREV" "$WRAPDIR" 2>/dev/null; return 0; }
trap cleanup EXIT

# --- 1. filelist -----------------------------------------------------------
echo "[1/3] bender script flist-plus -t $CVA6_TARGET"
( cd "$CVA6_ROOT" && "$BENDER" script flist-plus -t "$CVA6_TARGET" ) > "$FL.tmp" 2>"$FLDIR/bender.err" \
  || { cat "$FLDIR/bender.err" >&2; rm -f "$FL.tmp"; die "bender failed"; }
[[ -s "$FL.tmp" ]] || { rm -f "$FL.tmp"; die "bender produced an EMPTY filelist (it does this silently for an unknown --top)"; }
mv "$FL.tmp" "$FL"

# --- 2. elaborated AST -----------------------------------------------------
echo "[2/3] slang --top cva6 --ast-json-detailed-types"
"$SLANG" -f "$FL" --top cva6 --ast-json-detailed-types --ast-json "$AST.tmp" \
  > "$ASTDIR/slang.log" 2>&1
rc=$?
grep -q '^Build succeeded' "$ASTDIR/slang.log" || { tail -20 "$ASTDIR/slang.log" >&2; rm -f "$AST.tmp"; die "slang did not elaborate the core (rc=$rc)"; }
[[ -s "$AST.tmp" ]] || { rm -f "$AST.tmp"; die "slang produced no AST json"; }
mv "$AST.tmp" "$AST"

# --- 3. wrappers, into a FRESH staging directory ---------------------------
# Nothing from a previous run is visible to this step, so nothing from a
# previous run can be mistaken for output of this one.
echo "[3/3] gen_cva6_wrappers.py over $N_DECLARED declared modules (staging)"
rm -rf "$STAGE"; mkdir -p "$STAGE" || die "cannot create staging dir $STAGE"
python3 "$GEN" "$AST" --out "$STAGE" "${MODULES[@]}" > "$STAGE/gen.log" 2>&1 \
  || { tail -20 "$STAGE/gen.log" >&2; die "wrapper generation failed"; }

# Status comes from THIS invocation's explicit result line, one per declared
# module, and is then cross-checked against the staged file.  Either half alone
# is a trapdoor: the log alone cannot see a write that failed, and the file
# alone cannot see a SKIP.
printf 'module\twrapper_status\twrapper_sha256\treason\n' > "$WTSV.tmp"
n_gen=0; n_noinst=0; n_other=0
for m in "${MODULES[@]}"; do
  genline="$(grep -c "^  GEN  $m " "$STAGE/gen.log")"
  skipline="$(grep -c "^  SKIP $m " "$STAGE/gen.log")"
  [[ $((genline + skipline)) -eq 1 ]] || { rm -f "$WTSV.tmp"; die "generator gave $genline GEN and $skipline SKIP lines for '$m'; exactly one is required"; }
  w="$STAGE/${m}_gate.sv"
  if [[ "$genline" -eq 1 ]]; then
    [[ -s "$w" ]] || { rm -f "$WTSV.tmp"; die "generator reported GEN for '$m' but staged no file"; }
    printf '%s\tGENERATED\t%s\t-\n' "$m" "$(sha256sum < "$w" | cut -d' ' -f1)" >> "$WTSV.tmp"
    n_gen=$((n_gen + 1))
  else
    # A SKIP must leave NO file.  If one is there it is stale or stray, and the
    # corpus is not what the log says it is.
    [[ -e "$w" ]] && { rm -f "$WTSV.tmp"; die "generator reported SKIP for '$m' but a file is staged at $w"; }
    why="$(grep -m1 "^  SKIP $m " "$STAGE/gen.log" | sed "s/^  SKIP $m  *//")"
    if [[ "$why" == "no elaborated instance" ]]; then
      printf '%s\tNOT_IN_CONFIG\t-\t%s\n' "$m" "$why" >> "$WTSV.tmp"
      n_noinst=$((n_noinst + 1))
    else
      printf '%s\tNO_WRAPPER\t-\t%s\n' "$m" "${why:-unknown}" >> "$WTSV.tmp"
      n_other=$((n_other + 1))
    fi
  fi
done

# Nothing may be in the staged corpus that was not declared and generated.
while IFS= read -r f; do
  b="$(basename "$f")"; b="${b%_gate.sv}"
  grep -qP "^\Q$b\E\tGENERATED\t" "$WTSV.tmp" || { rm -f "$WTSV.tmp"; die "staged wrapper $f is not a declared GENERATED module"; }
done < <(find "$STAGE" -maxdepth 1 -name '*_gate.sv')

# --- publish the COMPLETE corpus, only now ---------------------------------
if [[ -d "$WRAPDIR" ]]; then mv "$WRAPDIR" "$PREV" || die "cannot move the old corpus aside"; fi
mv "$STAGE" "$WRAPDIR" || die "cannot publish the staged corpus"
trap - EXIT
rm -rf "$PREV"
mv "$WTSV.tmp" "$WTSV"

{
  echo "d4_head=$(cd "$ROOT" && git rev-parse HEAD)"
  echo "d4_tracked_dirty=$(cd "$ROOT" && [ -z "$(git status --porcelain --untracked-files=no)" ] && echo no || echo YES)"
  echo "worktree=$ROOT"
  echo "foundation_out=$OUTBASE"
  echo "module_list=$LIST"
  echo "module_list_sha256=$(sha256sum < "$LIST" | cut -d' ' -f1)"
  echo "module_count=$N_DECLARED"
  echo "cva6_root=$CVA6_ROOT"
  echo "cva6_root_canonical=$CVA6_ROOT_CANON"
  echo "cva6_rev=$CVA6_REV"
  echo "cva6_tracked_clean=yes"
  echo "cva6_target=$CVA6_TARGET"
  echo "cva6_config_pkg=$CFG"
  echo "cva6_config_pkg_sha256=$(sha256sum < "$CFG" | cut -d' ' -f1)"
  echo "bender=$BENDER"
  echo "bender_version=$("$BENDER" --version 2>&1 | head -1)"
  echo "filelist=$FL"
  echo "filelist_sha256=$(sha256sum < "$FL" | cut -d' ' -f1)"
  echo "filelist_lines=$(wc -l < "$FL")"
  echo "slang=$SLANG"
  echo "slang_version=$("$SLANG" --version 2>&1 | head -1)"
  echo "ast=$AST"
  echo "ast_sha256=$(sha256sum < "$AST" | cut -d' ' -f1)"
  echo "ast_bytes=$(stat -c%s "$AST")"
  echo "wrapper_dir=$WRAPDIR"
  echo "wrapper_generator=$GEN"
  echo "wrapper_generator_sha256=$(sha256sum < "$GEN" | cut -d' ' -f1)"
  # One value for the whole wrapper corpus, in declared order, over
  # "<module> <digest>" pairs.  Hashing `sha256sum <path>` OUTPUT would fold the
  # directory into the value; this worktree is reachable as both /soe/... and
  # /mada/..., so that hash would depend on which spelling was used.
  echo "wrappers_sha256=$(for m in "${MODULES[@]}"; do
          w="$WRAPDIR/${m}_gate.sv"
          if [[ -r "$w" ]]; then echo "$m $(sha256sum < "$w" | cut -d' ' -f1)"; else echo "$m -"; fi
        done | sha256sum | cut -d' ' -f1)"
  echo "wrappers_generated=$n_gen"
  echo "wrappers_not_in_config=$n_noinst"
  echo "wrappers_failed=$n_other"
  echo "lhd=$ROOT/bazel-bin/lhd/lhd"
  echo "lhd_sha256=$([[ -x "$ROOT/bazel-bin/lhd/lhd" ]] && sha256sum < "$ROOT/bazel-bin/lhd/lhd" | cut -d' ' -f1 || echo absent)"
  echo "lean_toolchain=$(cat "$ROOT/formal/lean/lean-toolchain" 2>/dev/null)"
  echo "lake_version=$( (cd "$ROOT/formal/lean" && "${LAKE:-$HOME/.elan/bin/lake}" --version 2>&1 | grep -m1 '^Lake version ') )"
  echo "built=$(date -Is)"
} > "$MAN"

printf 'wrappers: %s generated, %s not in config %s, %s failed, out of %s declared\n' \
  "$n_gen" "$n_noinst" "$CVA6_TARGET" "$n_other" "$N_DECLARED"
echo "manifest: $MAN"
[[ "$n_gen" -gt 0 ]] || exit 1
exit 0
