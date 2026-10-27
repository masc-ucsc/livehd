#!/usr/bin/env bash
# Build `//lhd:lhd` from the COMPLETE tree of an exact commit, in ISOLATION, and
# write a machine-checkable binding between (commit, configuration) and the
# resulting binary's SHA-256.
#
# WHY THIS EXISTS. Retaining a binary's hash, finding marker strings in it and
# copying `HEAD` into a header proves nothing about where those bytes came from.
# Review demonstrated it: a 419-byte shell wrapper carrying the expected marker
# strings and forwarding every command to the OTHER binary was published as
# `B_source_commit=45371d872`, with the wrapper's own hash retained, and the
# verifier accepted the table. The producer was attributing bytes to a commit by
# fiat.
#
# So the attribution is now something a machine can redo: check out the WHOLE
# tree of that commit into a throwaway worktree, build it there with the
# recorded configuration and its own output base, and record what came out.
# Nothing is built from a hand-picked subset of directories, and the active D4
# worktree is never modified.
#
#   d4_build_binding.sh build <commit> <attestation-out> [bazel flags...]
#   d4_build_binding.sh check <attestation>                # rebuild and compare
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
# shellcheck source=d4_evidence_provenance.sh
# `att_field`, `D4_BINDING_KEYS` and `d4_binding_validate` live THERE, not here.
# Both producers validate a binding before they measure with it, and a function
# that only this script defines made that call a no-op under `set -uo pipefail`:
# `command not found` is exit 127 on stderr and execution simply continues. The
# schema check is shared because both callers need it.
. "$HERE/d4_evidence_provenance.sh" || { echo "FATAL: cannot source the provenance library" >&2; exit 2; }

DEFAULT_CFG="-c dbg --repo_env=CC=/usr/bin/gcc-15 --repo_env=CXX=/usr/bin/g++-15"
WORK="$ROOT/temp/d4_build"
CACHE="${D4_BUILD_DISK_CACHE:-$WORK/disk_cache}"

# build_one <commit> <config> -> echoes "<sha256> <bytes> <retained-path>"
build_one() {
  local commit="$1" cfg="$2" sha12 wt ob rel eroot art sha
  sha12="${commit:0:12}"
  wt="$WORK/wt-$sha12"
  ob="$WORK/ob-$sha12"
  mkdir -p "$WORK" "$CACHE" || d4_die "cannot create $WORK"
  rm -rf "$wt"
  git -C "$ROOT" worktree prune
  git -C "$ROOT" worktree add --detach --quiet "$wt" "$commit" \
    || d4_die "cannot create an isolated worktree for $commit"
  # A COMPLETE tree, and provably so: the worktree's HEAD tree must be the
  # commit's tree, with nothing modified.
  local got want
  got="$(git -C "$wt" rev-parse 'HEAD^{tree}')"
  want="$(git -C "$ROOT" rev-parse "$commit^{tree}")"
  [[ "$got" == "$want" ]] || d4_die "the isolated worktree is not $commit's tree"
  [[ -z "$(git -C "$wt" status --porcelain --untracked-files=no)" ]] \
    || d4_die "the isolated worktree is not clean"

  # Its OWN output base, so nothing is shared with whatever the active session
  # last built -- the `bazel-bin` hazard that started this whole thread. The
  # disk cache is shared only to make a second commit's build cheap; it is
  # keyed by action, so it cannot substitute one commit's output for another's.
  # shellcheck disable=SC2086
  ( cd "$wt" && bazel --output_base="$ob" build $cfg --disk_cache="$CACHE" //lhd:lhd ) >"$WORK/$sha12.build.log" 2>&1 \
    || { tail -5 "$WORK/$sha12.build.log" >&2; d4_die "the isolated build of $commit failed"; }
  # shellcheck disable=SC2086
  rel="$( cd "$wt" && bazel --output_base="$ob" cquery $cfg --output=files //lhd:lhd 2>/dev/null | tail -1 )"
  # shellcheck disable=SC2086
  eroot="$( cd "$wt" && bazel --output_base="$ob" info $cfg execution_root 2>/dev/null )"
  [[ -n "$rel" && -n "$eroot" ]] || d4_die "cannot resolve the artifact built from $commit"
  art="$eroot/$rel"
  [[ -f "$art" && ! -L "$art" ]] || d4_die "$art is not a regular file"
  sha="$(d4_sha "$art")"
  local retained bytes
  retained="$(d4_retain_binary "$art")"
  bytes="$(stat -c %s "$art")"
  # Reclaim the space: the attestation and the retained binary are the outputs.
  ( cd "$wt" && bazel --output_base="$ob" shutdown ) >/dev/null 2>&1
  # bazel leaves parts of the output base read-only, so a plain `rm -rf` leaves
  # gigabytes behind on a shared filesystem.
  chmod -R u+w "$ob" >/dev/null 2>&1
  rm -rf "$ob"
  git -C "$ROOT" worktree remove --force "$wt" >/dev/null 2>&1
  printf '%s %s %s\n' "$sha" "$bytes" "$retained"
}

cmd="${1:?usage: d4_build_binding.sh build <commit> <out> [flags...] | check <attestation>}"
case "$cmd" in
build)
  COMMIT="${2:?missing commit}"; OUT="${3:?missing attestation path}"; shift 3
  CFG="${*:-$DEFAULT_CFG}"
  cd "$ROOT" || d4_die "cannot cd $ROOT"
  full="$(git rev-parse --verify --quiet "$COMMIT^{commit}")" || d4_die "$COMMIT is not a commit here"
  # `build_one` runs in a process substitution, so a `d4_die` inside it exits
  # only THAT subshell: `read` then sees EOF, leaves these empty, and -- in the
  # first version of this script -- an attestation full of blanks got written
  # anyway. Measured: interrupting a build produced a d4-binary-binding/1 file
  # with `sha256 =` and `bytes =` empty. An attestation is the whole point of
  # this script, so every field it asserts is checked before anything is
  # written, and a failed build publishes nothing.
  sha=""; bytes=""; retained=""
  read -r sha bytes retained < <(build_one "$full" "$CFG") || true
  [[ "$sha" =~ ^[0-9a-f]{64}$ ]] \
    || d4_die "the isolated build of $full produced no usable binary hash (got '${sha:-<nothing>}')"
  [[ "$bytes" =~ ^[1-9][0-9]*$ ]] \
    || d4_die "the isolated build of $full produced no byte count (got '${bytes:-<nothing>}')"
  [[ -n "$retained" && -f "$retained" ]] \
    || d4_die "the isolated build of $full retained nothing at '${retained:-<nothing>}'"
  [[ "$(d4_sha "$retained")" == "$sha" ]] \
    || d4_die "the retained copy of $full does not hash to $sha"
  mkdir -p "$(dirname "$OUT")"
  {
    echo "# schema   = d4-binary-binding/1"
    echo "commit     = $full"
    echo "tree       = $(git rev-parse "$full^{tree}")"
    echo "config     = $CFG"
    echo "bazel      = $(bazel --version 2>/dev/null | head -1)"
    echo "compiler   = $(/usr/bin/gcc-15 --version 2>/dev/null | head -1)"
    echo "sha256     = $sha"
    echo "bytes      = $bytes"
    echo "retained   = $retained"
  } > "$OUT.tmp.$$" || d4_die "cannot write $OUT"
  mv -f "$OUT.tmp.$$" "$OUT"
  echo "wrote $OUT"
  echo "  commit $full -> $sha"
  ;;
check)
  ATT="${2:?missing attestation}"
  cd "$ROOT" || d4_die "cannot cd $ROOT"
  # SCHEMA FIRST, then the rebuild. A binding that cannot be read exactly is
  # not evidence, whatever a rebuild would say about it.
  d4_binding_validate "$ATT"
  c="$(att_field "$ATT" commit)"; cfg="$(att_field "$ATT" config)"
  want="$(att_field "$ATT" sha256)"; wantb="$(att_field "$ATT" bytes)"
  ret="$(att_field "$ATT" retained)"
  sha=""; bytes=""
  read -r sha bytes _ < <(build_one "$c" "$cfg") || true
  [[ "$sha" =~ ^[0-9a-f]{64}$ ]] || d4_die "the rebuild of $c produced no usable binary hash"
  rc=0
  [[ "$sha"   == "$want"  ]] || { echo "MISMATCH: $c rebuilt to $sha, the binding claims $want" >&2; rc=1; }
  [[ "$bytes" == "$wantb" ]] || { echo "MISMATCH: $c rebuilt to $bytes bytes, the binding claims $wantb" >&2; rc=1; }
  # The retained artifact must be the bytes the binding is about.
  if [[ -f "$ret" ]]; then
    [[ "$(d4_sha "$ret")" == "$want" ]] \
      || { echo "MISMATCH: the retained artifact $ret does not hash to $want" >&2; rc=1; }
  else
    echo "MISSING: the retained artifact $ret is not present" >&2; rc=1
  fi
  [[ "$rc" -eq 0 ]] && echo "ok: $c with [$cfg] rebuilds to $want ($wantb bytes)"
  exit "$rc"
  ;;
*) d4_die "unknown command '$cmd'" ;;
esac
