#!/usr/bin/env bash
# Run a command with LHD bound to a SPECIFIC binary, asserting its SHA-256
# before and after.
#
# WHY. `bazel-bin` is a convenience symlink that follows the LAST-BUILT
# configuration in a shared output base, and this machine runs concurrent bazel
# sessions. Hashing or running through it proves nothing about which binary a
# measurement used -- which is how an earlier unit's evidence ended up bound to
# a binary that did not contain the implementation it claimed to authenticate.
set -uo pipefail
EXPECT="${1:?expected sha256}"; shift
ART="${1:?binary path}"; shift
got="$(sha256sum "$ART" | cut -d' ' -f1)"
[[ "$got" == "$EXPECT" ]] || { echo "FATAL: $ART is $got, expected $EXPECT" >&2; exit 2; }
LHD="$ART" "$@"
rc=$?
got="$(sha256sum "$ART" | cut -d' ' -f1)"
[[ "$got" == "$EXPECT" ]] || { echo "FATAL: $ART CHANGED during the run: $got" >&2; exit 2; }
exit $rc
