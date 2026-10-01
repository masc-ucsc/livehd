#!/usr/bin/env bash
# Drift check for the shared semantics, INCLUDING the one local overlay.
#
# Seven of the eight pinned files must be byte-identical to the semantic base.
# `Translation/LGraphModel.lean` is that base PLUS `bv_shl_step.patch`, and is
# checked two independent ways: against a recorded hash, and by regenerating it
# from the base.  The hash arm still means something when the base repository is
# unreachable, so the record does not depend on anything outside this repo.
#
# Runnable from ANY directory: the repo root is derived from this script's own
# location, never from `git rev-parse`.
#
# Scratch space is PROJECT-LOCAL by default ($ROOT/.runtime/overlay_check) and
# never /tmp.  $TMPDIR is honoured if set, including to a path inside this
# worktree -- regeneration uses patch(1) rather than `git apply`, so there is no
# parent-repository discovery to be confused by a scratch dir inside the tree.
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../../../.." && pwd)

BASE_REV=90583be1349acbd25f740b176a406aa8bbe1251c
BASE_REPO=${BASE_REPO:-/mada/users/czeng14/projects/livehd-d2-ir-semantics}
SRC=$ROOT/formal/lean/LeanSemanticPrimitives
PATCHFILE=$HERE/bv_shl_step.patch
EXPECTED_LGRAPHMODEL_SHA256=8a4fc7ea4132b3d845a8871c614bd36fa6d710ce1c63c2d783b81525813d6c4b

rc=0

echo "-- seven pinned files, expected byte-identical to $BASE_REV"
for f in Translation/GraphRefine.lean Translation/OpBridge.lean \
         Compiler/DesignCert.lean Compiler/Runtime.lean \
         Compiler/DesignCertWF.lean Compiler/DesignSemantics.lean \
         SemanticPrimitives.lean; do
  if (cd "$BASE_REPO" && git show "$BASE_REV:formal/lean/LeanSemanticPrimitives/$f") \
       | cmp -s - "$SRC/$f"; then echo "  ok     $f"; else echo "  DRIFT  $f"; rc=1; fi
done

echo "-- one overlaid file"
got=$(sha256sum "$SRC/Translation/LGraphModel.lean" | cut -d' ' -f1)
if [ "$got" = "$EXPECTED_LGRAPHMODEL_SHA256" ]; then
  echo "  ok     Translation/LGraphModel.lean (hash)"
else
  echo "  DRIFT  Translation/LGraphModel.lean (hash)"
  echo "         expected $EXPECTED_LGRAPHMODEL_SHA256"
  echo "         got      $got"
  rc=1
fi

if ! command -v patch >/dev/null 2>&1; then
  echo "  skip   regeneration: patch(1) not found; hash check above still applies"
else
  WORK=${TMPDIR:-$ROOT/.runtime/overlay_check}
  mkdir -p "$WORK" || { echo "  DRIFT  cannot create $WORK"; exit 1; }
  tmp=$(mktemp -d "$WORK/overlay-check.XXXXXX") || exit 1
  trap 'rm -rf "$tmp"' EXIT
  if (cd "$BASE_REPO" && git show "$BASE_REV:formal/lean/LeanSemanticPrimitives/Translation/LGraphModel.lean") \
       > "$tmp/LGraphModel.lean" 2>/dev/null; then
    mkdir -p "$tmp/w/formal/lean/LeanSemanticPrimitives/Translation"
    cp "$tmp/LGraphModel.lean" "$tmp/w/formal/lean/LeanSemanticPrimitives/Translation/LGraphModel.lean"
    # patch(1), NOT `git apply`: no repository discovery, so $tmp may live
    # anywhere, including inside this worktree
    if patch -p1 -s -d "$tmp/w" < "$PATCHFILE" \
         && cmp -s "$tmp/w/formal/lean/LeanSemanticPrimitives/Translation/LGraphModel.lean" \
                   "$SRC/Translation/LGraphModel.lean"; then
      echo "  ok     Translation/LGraphModel.lean (base + bv_shl_step.patch)"
    else
      echo "  DRIFT  Translation/LGraphModel.lean (base + bv_shl_step.patch)"
      rc=1
    fi
  else
    echo "  skip   base unreachable at $BASE_REPO; hash check above still applies"
  fi
fi

[ $rc -eq 0 ] && echo "overlay check: OK" || echo "overlay check: FAILED"
exit $rc
