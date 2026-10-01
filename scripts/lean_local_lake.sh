#!/usr/bin/env bash
# Create (or re-verify) a Lake build tree that belongs to THIS worktree.
#
# THE BUG THIS PREVENTS. `formal/lean/.lake` shipped as a SYMLINK into another
# worktree, so two branches shared one Lake build tree. The other branch's
# `DesignCert` has no `clocks` field, so a build there replaced the oleans this
# branch needs and every module failed with
#
#     error: `clocks` is not a field of structure `DesignCert`
#
# while the stale .olean's mtime was NEWER than the source, so the ordinary
# staleness check said it was fine. The artifacts that collided are this
# project's OWN library oleans (LeanSemanticPrimitives/**), and those are what
# this script makes local.
#
# DEPENDENCIES (mathlib et al., ~7.6 GB) are copied with `--reflink=auto`. On
# btrfs that is copy-on-write: distinct inodes, shared extents, no disk cost,
# and a write through one tree CANNOT affect the other. If the filesystem does
# not support reflinks the copy is a real one, and if that is refused the
# script says so rather than silently falling back to hardlinks -- a hardlink
# is NOT an isolation boundary, because an in-place write hits both trees.
#
# It never writes to, or removes, the shared tree: only this worktree's symlink
# is replaced.
#
# usage: scripts/lean_local_lake.sh [--verify-only]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOCAL="$ROOT/generated/lean_validation/lake"
LINK="$ROOT/formal/lean/.lake"
VERIFY_ONLY="${1:-}"

note() { printf '  %s\n' "$*"; }

# Where the pinned dependency tree lives: whatever the symlink used to point at,
# else the recorded value, else the conventional sibling worktree.
SHARED=""
if [[ -L "$LINK" ]]; then
  t="$(readlink -f "$LINK")"
  [[ "$t" == "$LOCAL" ]] || SHARED="$t"
fi
[[ -n "$SHARED" ]] || SHARED="$(cat "$ROOT/generated/lean_validation/PREVIOUS_LAKE_TARGET.txt" 2>/dev/null || true)"
[[ -n "$SHARED" ]] || SHARED="/mada/users/czeng14/projects/livehd-new/formal/lean/.lake"

if [[ "$VERIFY_ONLY" != "--verify-only" ]]; then
  if [[ ! -d "$LOCAL/packages" ]]; then
    [[ -d "$SHARED/packages" ]] || { echo "FATAL: no dependency tree at $SHARED/packages"; exit 2; }
    mkdir -p "$ROOT/generated/lean_validation"/{logs,probes}
    note "copying deps from $SHARED/packages (reflink where supported)"
    # -a preserves everything; --reflink=auto is CoW on btrfs and a plain copy
    # elsewhere. Never -l: a hardlink is not an isolation boundary.
    cp -a --reflink=auto "$SHARED/packages" "$LOCAL/packages"
    [[ -d "$SHARED/config" ]] && cp -a --reflink=auto "$SHARED/config" "$LOCAL/config" || true
    mkdir -p "$LOCAL/build"
    echo "$SHARED" > "$ROOT/generated/lean_validation/PREVIOUS_LAKE_TARGET.txt"
  else
    note "local lake tree already present"
  fi
  if [[ "$(readlink -f "$LINK" 2>/dev/null)" != "$LOCAL" ]]; then
    note "repointing $LINK -> generated/lean_validation/lake"
    rm -f "$LINK"          # removes only THIS worktree's symlink
    ln -s ../../generated/lean_validation/lake "$LINK"
  fi
fi

# ---- verification: the claim is distinct inodes, so check distinct inodes ----
fail=0
resolved="$(readlink -f "$LINK" 2>/dev/null || echo '<none>')"
[[ "$resolved" == "$LOCAL" ]] || { echo "FAIL: $LINK resolves to $resolved, not $LOCAL"; fail=1; }

shared_cnt=0 checked=0
while IFS= read -r rel; do
  a="$LOCAL/packages/$rel"; b="$SHARED/packages/$rel"
  [[ -f "$a" && -f "$b" ]] || continue
  checked=$((checked+1))
  [[ "$(stat -c '%i' "$a")" == "$(stat -c '%i' "$b")" ]] && shared_cnt=$((shared_cnt+1))
done < <(cd "$LOCAL/packages" 2>/dev/null && find . -name '*.olean' -printf '%P\n' 2>/dev/null | head -200)

if [[ "$checked" -gt 0 ]]; then
  if [[ "$shared_cnt" -gt 0 ]]; then
    echo "FAIL: $shared_cnt of $checked sampled dependency .olean files SHARE AN INODE with"
    echo "      $SHARED/packages -- that is a hardlink cache, not isolation. Re-create with"
    echo "      a reflink or real copy."
    fail=1
  else
    note "sampled $checked dependency .olean files: all distinct inodes from the shared tree"
  fi
fi

own="$LOCAL/build/lib/lean/LeanSemanticPrimitives/Compiler/DesignCert.olean"
if [[ -f "$own" ]]; then
  note "own-library oleans are local: $own"
  o="$SHARED/build/lib/lean/LeanSemanticPrimitives/Compiler/DesignCert.olean"
  if [[ -f "$o" && "$(stat -c '%i' "$own")" == "$(stat -c '%i' "$o")" ]]; then
    echo "FAIL: this branch's own DesignCert.olean still shares an inode with the shared tree"
    fail=1
  fi
else
  note "own-library oleans not built yet (run: cd formal/lean && lake build)"
fi

[[ "$fail" -eq 0 ]] || exit 1
echo "OK: lake build tree is local to this worktree"
