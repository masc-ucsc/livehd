#!/usr/bin/env bash
# Drift check for the shared semantics the projection consumes on master.
#
#   check_overlay.sh [--semantic]
#
# Six files must be byte-identical to the semantic base 90583be (present in
# this repository; BASE_REPO overrides).  Two are MASTER-ADAPTED --
# Translation/LGraphModel.lean (the SHL/SRA/Sext boundary repairs) and
# Translation/OpBridge.lean (the bridges re-proved for them) -- and are checked
# against recorded sha256 values.  Changing either is legitimate, but must
# update the record here in the same commit (see SHARED_SEMANTICS.md).
#
# --semantic additionally re-checks the bridge THEOREMS rather than trusting the
# record: it rebuilds Projection/OperatorBridge (no Mathlib) and, under lake,
# Translation/OpBridge (Mathlib).
#
# overlays/bv_shl_step.patch is the FP branch's historical overlay; master did
# not apply it (its body is the unguarded 2^k), so it is no longer regenerated.
#
# Runnable from ANY directory: the repo root comes from this script's location.
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../../../.." && pwd)

BASE_REV=90583be1349acbd25f740b176a406aa8bbe1251c
BASE_REPO=${BASE_REPO:-$ROOT}
SRC=$ROOT/formal/lean/LeanSemanticPrimitives

# master-adapted files: path, recorded sha256
EXPECTED_LGRAPHMODEL_SHA256=432bb98394892ad67d156923197ed90945f57cfbf86f3e6d2b602eead0f173ba
EXPECTED_OPBRIDGE_SHA256=fc9366004cb12f1d96ae8624aaea4187960939d67c5b2b0c0b3bd3455d853b77

SEMANTIC=0
[ "${1:-}" = "--semantic" ] && SEMANTIC=1

rc=0

echo "-- six files, expected byte-identical to $BASE_REV"
if ! (cd "$BASE_REPO" && git cat-file -e "$BASE_REV^{commit}") 2>/dev/null; then
  echo "  FAIL   base $BASE_REV not found in $BASE_REPO"; rc=1
else
  for f in Translation/GraphRefine.lean \
           Compiler/DesignCert.lean Compiler/Runtime.lean \
           Compiler/DesignCertWF.lean Compiler/DesignSemantics.lean \
           SemanticPrimitives.lean; do
    if (cd "$BASE_REPO" && git show "$BASE_REV:formal/lean/LeanSemanticPrimitives/$f") \
         | cmp -s - "$SRC/$f"; then echo "  ok     $f"; else echo "  DRIFT  $f"; rc=1; fi
  done
fi

echo "-- two master-adapted files, against recorded hashes"
check_hash() {   # path expected
  local got; got=$(sha256sum "$SRC/$1" | cut -d' ' -f1)
  if [ "$got" = "$2" ]; then
    echo "  ok     $1"
  else
    echo "  DRIFT  $1"
    echo "         expected $2"
    echo "         got      $got"
    rc=1
  fi
}
check_hash Translation/LGraphModel.lean "$EXPECTED_LGRAPHMODEL_SHA256"
check_hash Translation/OpBridge.lean    "$EXPECTED_OPBRIDGE_SHA256"

if [ $SEMANTIC -eq 1 ]; then
  echo "-- the bridge theorems, rebuilt"
  if (cd "$ROOT/formal/lean" && bash .build-proj.sh SemanticPrimitives Translation/LGraphModel \
        Translation/GraphRefine Compiler/DesignCert Compiler/Runtime Compiler/DesignCertWF \
        Compiler/DesignSemantics Projection/ObjectLanguage Projection/ObjectLanguageSemantics \
        Projection/OperatorBridge) | grep -q 'Projection/OperatorBridge *ok'; then
    echo "  ok     Projection/OperatorBridge (bv_shl_step_unfolds, evalOp_SHL/denoteOp_SHL_unchanged_by_overlay, evalOp_Sext)"
  else
    echo "  FAIL   Projection/OperatorBridge did not build"; rc=1
  fi
  if (cd "$ROOT/formal/lean" && lake build LeanSemanticPrimitives.Translation.OpBridge >/dev/null 2>&1); then
    echo "  ok     Translation/OpBridge (bv_sra_guard_eq and the per-operator bridges)"
  else
    echo "  FAIL   Translation/OpBridge did not build under lake"; rc=1
  fi
fi

[ $rc -eq 0 ] && echo "overlay check: OK" || echo "overlay check: FAILED"
exit $rc
