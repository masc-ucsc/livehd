#!/usr/bin/env bash
# Drift check for the shared semantics the projection consumes on master.
#
#   check_overlay.sh [--semantic]
#
# Four files must be byte-identical to the semantic base 90583be (present in
# this repository; BASE_REPO overrides).  One is a COMMENT-ONLY adaptation --
# Compiler/DesignCert.lean, whose ClockDesc docstring 032690762 corrected -- and
# must equal the base once Lean comments are removed AND match a recorded
# sha256, so a code change fails the first test and any further edit, even to a
# comment, has to update the record.  One is an IMPORT-ONLY adaptation --
# Compiler/Runtime.lean, which imports Translation/FastBV so the code compiled
# in it and in every module that imports it uses the proved @[csimp] fast
# forms -- and must equal the base once that one import line is removed AND
# match a recorded sha256, the same way.  Two are MASTER-ADAPTED --
# Translation/LGraphModel.lean (the SHL/SRA/Sext boundary repairs) and
# Translation/OpBridge.lean (the bridges re-proved for them) -- and are checked
# against recorded sha256 values.  Changing either is legitimate, but must
# update the record here in the same commit (see SHARED_SEMANTICS.md).
#
# --semantic additionally re-checks the bridge THEOREMS rather than trusting the
# record: it rebuilds Projection/OperatorBridge (no Mathlib) and, under lake,
# Translation/OpBridge (Mathlib).
#
# evidence/bv_shl_step.patch is the FP branch's historical overlay; master did
# not apply it (its body is the unguarded 2^k), so it is no longer regenerated.
#
# Runnable from ANY directory: the repo root comes from this script's location.
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)

BASE_REV=90583be1349acbd25f740b176a406aa8bbe1251c
BASE_REPO=${BASE_REPO:-$ROOT}
SRC=$ROOT/formal/lean/LeanSemanticPrimitives

# master-adapted files: path, recorded sha256
EXPECTED_LGRAPHMODEL_SHA256=432bb98394892ad67d156923197ed90945f57cfbf86f3e6d2b602eead0f173ba
EXPECTED_OPBRIDGE_SHA256=fc9366004cb12f1d96ae8624aaea4187960939d67c5b2b0c0b3bd3455d853b77
# comment-only adaptation: path, recorded sha256
EXPECTED_DESIGNCERT_SHA256=c622029ee92ef9a954af643584bd56a328c759bbf02a344650f90481cb8b13f9
# import-only adaptation: the one added line, recorded sha256
RUNTIME_ADDED_IMPORT='import LeanSemanticPrimitives.Translation.FastBV'
EXPECTED_RUNTIME_SHA256=ba7b341173caf8f6925ca57477634b2e72c987f4893d777332628445a94de3e8

SEMANTIC=0
[ "${1:-}" = "--semantic" ] && SEMANTIC=1

rc=0

# Lean source with every comment removed: `--` to end of line and `/- ... -/`
# blocks (which nest, docstrings `/-- -/` and `/-! -/` included). String
# literals are skipped so a `--` inside one is not taken as a comment.
strip_lean_comments() {
  python3 -c '
import sys
s = sys.stdin.read()
out, i, depth, n = [], 0, 0, len(s)
while i < n:
    if depth == 0 and s[i] == "\"":
        j = i + 1
        while j < n and s[j] != "\"":
            j += 2 if s[j] == "\\" else 1
        out.append(s[i:j + 1]); i = j + 1
    elif s.startswith("/-", i):
        depth += 1; i += 2
    elif depth > 0 and s.startswith("-/", i):
        depth -= 1; i += 2
    elif depth > 0:
        i += 1
    elif s.startswith("--", i):
        while i < n and s[i] != "\n":
            i += 1
    else:
        out.append(s[i]); i += 1
print("\n".join(l.rstrip() for l in "".join(out).splitlines() if l.strip()))
'
}

echo "-- four files, expected byte-identical to $BASE_REV"
if ! (cd "$BASE_REPO" && git cat-file -e "$BASE_REV^{commit}") 2>/dev/null; then
  echo "  FAIL   base $BASE_REV not found in $BASE_REPO"; rc=1
else
  for f in Translation/GraphRefine.lean \
           Compiler/DesignCertWF.lean Compiler/DesignSemantics.lean \
           SemanticPrimitives.lean; do
    if (cd "$BASE_REPO" && git show "$BASE_REV:formal/lean/LeanSemanticPrimitives/$f") \
         | cmp -s - "$SRC/$f"; then echo "  ok     $f"; else echo "  DRIFT  $f"; rc=1; fi
  done
  echo "-- one comment-only adaptation: equal to $BASE_REV without comments, and a recorded hash"
  f=Compiler/DesignCert.lean
  if cmp -s <(cd "$BASE_REPO" && git show "$BASE_REV:formal/lean/LeanSemanticPrimitives/$f" | strip_lean_comments) \
            <(strip_lean_comments < "$SRC/$f"); then
    echo "  ok     $f (no code change against the base)"
  else
    echo "  DRIFT  $f differs from the base OUTSIDE comments"; rc=1
  fi
  echo "-- one import-only adaptation: equal to $BASE_REV without its one added import, and a recorded hash"
  f=Compiler/Runtime.lean
  if [ "$(grep -c -x -F "$RUNTIME_ADDED_IMPORT" "$SRC/$f")" != 1 ]; then
    echo "  DRIFT  $f does not carry exactly one '$RUNTIME_ADDED_IMPORT'"; rc=1
  elif cmp -s <(cd "$BASE_REPO" && git show "$BASE_REV:formal/lean/LeanSemanticPrimitives/$f") \
              <(grep -v -x -F "$RUNTIME_ADDED_IMPORT" "$SRC/$f"); then
    echo "  ok     $f (no change against the base but the import)"
  else
    echo "  DRIFT  $f differs from the base in more than the added import"; rc=1
  fi
fi

echo "-- recorded hashes: two master-adapted files, the comment-only one and the import-only one"
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
check_hash Compiler/DesignCert.lean     "$EXPECTED_DESIGNCERT_SHA256"
check_hash Compiler/Runtime.lean        "$EXPECTED_RUNTIME_SHA256"

if [ $SEMANTIC -eq 1 ]; then
  echo "-- the bridge theorems, rebuilt"
  # OperatorBridge's whole import closure, in dependency order: .build-proj.sh builds
  # only what it is given, so a missing import passed only when an earlier full
  # build had left its .olean behind.
  if (cd "$ROOT/formal/lean" && bash .build-proj.sh Projection/ObjectLanguage Projection/BindingTime \
        Projection/Encoding SemanticPrimitives Translation/LGraphModel Compiler/DesignCert \
        Projection/DesignEncoding Translation/FastBV Projection/ObjectLanguageSemantics Translation/GraphRefine \
        Compiler/DesignCertWF Compiler/Runtime Compiler/DesignSemantics Projection/SimulatorContract \
        Projection/RuntimeEncoding Projection/OperatorBridge) | grep -q 'Projection/OperatorBridge *ok'; then
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
