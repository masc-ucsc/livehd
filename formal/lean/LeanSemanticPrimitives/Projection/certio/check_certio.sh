#!/usr/bin/env bash
# Provenance check for the ported DCERT1 loader.
#
# `Compiler/CertIO.lean` here is NOT byte-identical to its source: §4 and
# `runCycles` were deliberately dropped, the import was retargeted, the header
# was rewritten, and §6 (`asyncOK`) was added.  So a byte comparison would be
# meaningless.  What IS checked is that THE THING WE PORTED FROM has not moved:
# the pinned d4 commit still exists and its CertIO.lean still hashes to what it
# hashed to when the port was made.
#
# Runnable from any directory; the repo root comes from this script's location.
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../../../.." && pwd)

SRC_REPO=${SRC_REPO:-/mada/users/czeng14/projects/livehd-d4-incremental}
SRC_REV=c63b21cea2adff657083af02e34e680268692e42
SRC_PATH=formal/lean/LeanSemanticPrimitives/Compiler/CertIO.lean
EXPECTED_SHA256=26b2f61c722f377128b6137bcedfcb0fa060b1a23d26b80348c2b2fa6ef02994

rc=0
echo "-- pinned source: $SRC_REPO @ $SRC_REV"
if [ ! -d "$SRC_REPO/.git" ] && [ ! -f "$SRC_REPO/.git" ]; then
  echo "  SKIP   source repository not present; the recorded hash stands unverified"
  exit 0
fi
got=$( (cd "$SRC_REPO" && git show "$SRC_REV:$SRC_PATH" 2>/dev/null) | sha256sum | cut -d' ' -f1 )
if [ "$got" = "$EXPECTED_SHA256" ]; then
  echo "  ok     $SRC_PATH (sha256 $got)"
else
  echo "  DRIFT  $SRC_PATH"
  echo "         expected $EXPECTED_SHA256"
  echo "         got      $got"
  rc=1
fi

echo "-- what was intentionally NOT ported (must stay absent here)"
for sym in runChecked runChecked_correct loadAndRun runCycles; do
  if grep -qE "^def $sym|^theorem $sym" "$ROOT/formal/lean/LeanSemanticPrimitives/Compiler/CertIO.lean"; then
    echo "  UNEXPECTED  $sym is present; it is d4's compiler path"; rc=1
  else
    echo "  ok     $sym absent"
  fi
done

[ $rc -eq 0 ] && echo "certio check: OK" || echo "certio check: FAILED"
exit $rc
