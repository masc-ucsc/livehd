#!/usr/bin/env bash
# Resolve the `lhd` build artifact for an EXPLICIT bazel configuration and copy
# it to a fixed path, printing "<path> <sha256>".
#
# NEVER `bazel-bin/lhd/lhd`. That is a convenience symlink which follows the
# LAST-BUILT CONFIGURATION in the output base, and this machine runs concurrent
# bazel sessions against one base -- `k8-dbg` and `k8-fastbuild` trees both
# exist here. Two units' evidence was already bound to the wrong binary that
# way. The configuration is part of the identity, so it is passed in, recorded,
# and used for BOTH the build and the resolve.
#
# Usage: resolve_lhd_artifact.sh <dest> [bazel-config-flags...]
#   default flags: -c dbg --repo_env=CC=/usr/bin/gcc-15 --repo_env=CXX=/usr/bin/g++-15
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
die() { printf 'FATAL: %s\n' "$*" >&2; exit 2; }

DEST="${1:?usage: resolve_lhd_artifact.sh <dest> [bazel flags...]}"; shift
if [[ "$#" -eq 0 ]]; then
  set -- -c dbg --repo_env=CC=/usr/bin/gcc-15 --repo_env=CXX=/usr/bin/g++-15
fi
cd "$ROOT" || die "cannot cd $ROOT"

bazel build "$@" //lhd:lhd >/dev/null 2>&1 || die "bazel build //lhd:lhd failed for: $*"
rel="$(bazel cquery "$@" --output=files //lhd:lhd 2>/dev/null | tail -1)"
[[ -n "$rel" ]] || die "bazel cquery produced no artifact path for: $*"
eroot="$(bazel info "$@" execution_root 2>/dev/null)"
[[ -n "$eroot" ]] || die "bazel info execution_root failed for: $*"
art="$eroot/$rel"
[[ -f "$art" && ! -L "$art" ]] || die "$art is not a regular file"

mkdir -p "$(dirname "$DEST")" || die "cannot create $(dirname "$DEST")"
before="$(sha256sum "$art" | cut -d' ' -f1)"
cp -f "$art" "$DEST" || die "cannot copy $art to $DEST"
chmod u+w "$DEST"
after="$(sha256sum "$DEST" | cut -d' ' -f1)"
[[ "$before" == "$after" ]] || die "the artifact changed while being copied ($before -> $after)"
# And it must still be that after the copy, or something else is building.
[[ "$(sha256sum "$art" | cut -d' ' -f1)" == "$before" ]] \
  || die "the resolved artifact changed underneath this script; another build is running"
printf '%s %s\n' "$DEST" "$after"
