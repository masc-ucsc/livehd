#!/usr/bin/env bash
# Provenance check for FP's DCERT1 codec, as master carries it.
#
# On master FP's codec is Compiler/CertIOV1.lean (namespace Compiler.CertIO.V1):
# the FP branch's Compiler/CertIO.lean, verbatim apart from the namespace and a
# §7 proving the V1 renderer is not injective over clock metadata.  The
# unqualified Compiler.CertIO is DCERT2 (CertIOCodec + CertIO), a different
# codec.  Checked here:
#
#   1. LOCAL, always: the V1 codec does not contain d4's compiler path
#      (runChecked, runChecked_correct, loadAndRun, runCycles) -- FP ported the
#      format and the reader, never that path.
#   2. PROVENANCE: the d4 commit FP ported from still exists and its CertIO.lean
#      still hashes to what it hashed to when the port was made; the FP pin's
#      own CertIO.lean likewise.
#
# Both commits are present in this repository, so SRC_REPO defaults to it.
# Runnable from any directory; the repo root comes from this script's location.
set -uo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)

SRC_REPO=${SRC_REPO:-$ROOT}
D4_REV=c63b21cea2adff657083af02e34e680268692e42
D4_SHA256=26b2f61c722f377128b6137bcedfcb0fa060b1a23d26b80348c2b2fa6ef02994
FP_PIN=9bb41a09d6b57111ed2b3fd10102ec4ff174b6c6
CERTIO_PATH=formal/lean/LeanSemanticPrimitives/Compiler/CertIO.lean
V1="$ROOT/formal/lean/LeanSemanticPrimitives/Compiler/CertIOV1.lean"

rc=0

echo "-- local: what FP intentionally did NOT port must stay absent from CertIOV1"
if [ ! -f "$V1" ]; then
  echo "  FAIL   $V1 not found"; rc=1
else
  for sym in runChecked runChecked_correct loadAndRun runCycles; do
    if grep -qE "^(def|theorem) $sym\b" "$V1"; then
      echo "  UNEXPECTED  $sym is present in CertIOV1; it is d4's compiler path"; rc=1
    else
      echo "  ok     $sym absent"
    fi
  done
fi

echo "-- provenance (in $SRC_REPO)"
hash_at() { (cd "$SRC_REPO" && git show "$1:$CERTIO_PATH" 2>/dev/null) | sha256sum | cut -d' ' -f1; }
if ! (cd "$SRC_REPO" && git cat-file -e "$D4_REV^{commit}") 2>/dev/null; then
  echo "  SKIP   d4 source commit $D4_REV not present; the recorded hash stands unverified"
else
  got=$(hash_at "$D4_REV")
  if [ "$got" = "$D4_SHA256" ]; then echo "  ok     d4 $D4_REV CertIO.lean (sha256 $got)"
  else echo "  DRIFT  d4 $D4_REV CertIO.lean: expected $D4_SHA256 got $got"; rc=1; fi
fi
if ! (cd "$SRC_REPO" && git cat-file -e "$FP_PIN^{commit}") 2>/dev/null; then
  echo "  SKIP   FP pin $FP_PIN not present"
else
  echo "  ok     FP pin $FP_PIN present (its CertIO.lean is CertIOV1's source, sha256 $(hash_at "$FP_PIN"))"
fi

[ $rc -eq 0 ] && echo "certio check: OK" || echo "certio check: FAILED"
exit $rc
