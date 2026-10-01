#!/usr/bin/env bash
# Drift check for the shared semantics, INCLUDING the one local overlay.
#
# Seven of the eight pinned files must be byte-identical to the semantic base.
# `Translation/LGraphModel.lean` is that base PLUS `bv_shl_step.patch`, and is
# checked two independent ways: by regenerating it from the base, and against a
# recorded hash so the check still means something if the base is unreachable.
#
# Run from the repo root.
set -uo pipefail

BASE_REV=90583be1349acbd25f740b176a406aa8bbe1251c
BASE_REPO=${BASE_REPO:-/mada/users/czeng14/projects/livehd-d2-ir-semantics}
SRC=formal/lean/LeanSemanticPrimitives
OVL=$SRC/Projection/overlays
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

tmp=$(mktemp -d "${TMPDIR:-/tmp}/overlay-check.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
if (cd "$BASE_REPO" && git show "$BASE_REV:formal/lean/LeanSemanticPrimitives/Translation/LGraphModel.lean") \
     > "$tmp/base.lean" 2>/dev/null; then
  mkdir -p "$tmp/w/formal/lean/LeanSemanticPrimitives/Translation"
  cp "$tmp/base.lean" "$tmp/w/formal/lean/LeanSemanticPrimitives/Translation/LGraphModel.lean"
  if (cd "$tmp/w" && git apply "$OLDPWD/$OVL/bv_shl_step.patch" 2>/dev/null) \
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

[ $rc -eq 0 ] && echo "overlay check: OK" || echo "overlay check: FAILED"
exit $rc
