#!/usr/bin/env bash
# Provenance every front-end evidence table must carry, written by the PRODUCER
# into the table itself -- never pasted in afterwards.
#
# WHY THIS IS A SHARED FILE. Two tables are produced by two scripts and checked
# by one verifier. A provenance block that each writes its own way is a block
# the verifier can only check loosely, and the failure this exists to prevent is
# precisely a table whose header says something no tool ever established.
#
# WHAT WENT WRONG BEFORE, twice:
#   * a hash was taken from `bazel-bin/lhd/lhd`, a convenience symlink that
#     follows the LAST-BUILT CONFIGURATION in a shared output base -- so it
#     named whichever binary some other invocation had built last;
#   * the replacement wrote hashes to STDOUT and left the table's header as
#     hand-written prose, including a marker claim that was not true of either
#     binary. Review reproduced both.
#
# So: the producer asserts, the producer writes, and the verifier requires.
set -uo pipefail

d4_die() { printf 'FATAL: %s\n' "$*" >&2; exit 2; }

# d4_sha <file>
d4_sha() { sha256sum "$1" | cut -d' ' -f1; }

# A binary this evidence may be attributed to: it must exist, be executable,
# and be a REGULAR file -- not a symlink that can be repointed under us.
# d4_bin_check <path> <label>
d4_bin_check() {
  local p="$1" l="$2"
  [[ -n "$p" ]]                || d4_die "$l: no binary given"
  [[ -e "$p" ]]                || d4_die "$l: $p does not exist"
  [[ -f "$p" && ! -L "$p" ]]   || d4_die "$l: $p is not a regular file (a symlink is a path, not an identity)"
  [[ -x "$p" ]]                || d4_die "$l: $p is not executable"
}

# The source tree this binary may be attributed to. Modifications are allowed
# ONLY under the evidence directory -- a table and its own prose change in the
# same commit that publishes them, but a change to anything that COMPILES INTO
# the binary means the binary is not the committed source.
# d4_require_clean_source <evidence-dir-relative-to-root>
d4_require_clean_source() {
  local ev="${1:-pass/lean/Lgraph_IR_Semantics_Simulator/tests/d4}"
  git rev-parse --is-inside-work-tree >/dev/null 2>&1 || d4_die "not inside a git work tree"
  local dirty
  dirty="$(git diff --name-only HEAD -- . ":!$ev" | tr '\n' ' ')"
  [[ -z "$dirty" ]] || d4_die "source outside $ev is modified, so no binary here is the committed source: $dirty"
}

# d4_marker_state <binary> <needle> -> present|absent
#
# `grep -c`, not `grep -q`: under `pipefail` a `-q` exits on the first match and
# SIGPIPEs `strings`, so the PIPELINE reports failure and a marker that IS there
# reads as absent. Measured here -- the first run of this very check reported
# `gated-clock-latch-unsupported` absent from a binary that prints it.
d4_marker_state() {
  local cache n
  cache="$(d4_marker_cache "$1")"
  n="$(grep -cF -- "$2" "$cache")"
  if [[ "${n:-0}" -gt 0 ]]; then echo present; else echo absent; fi
}

# One `strings` pass per binary, kept under the project (never /tmp) and keyed
# by the binary's own hash, so a different binary can never reuse the dump.
# Without this, probing five markers across two 1.26 GB binaries meant ten full
# scans and the producer spent longer deciding what the binaries ARE than
# measuring what they DO.
d4_marker_cache() {
  local bin="$1" sha dir out
  sha="$(d4_sha "$bin")"
  dir="${D4_MARKER_CACHE_DIR:-$(git rev-parse --show-toplevel)/temp/marker_cache}"
  mkdir -p "$dir" || d4_die "cannot create the marker cache at $dir"
  out="$dir/$sha.strings"
  if [[ ! -s "$out" ]]; then
    strings -a "$bin" > "$out.$$" 2>/dev/null || true
    [[ -s "$out.$$" ]] || d4_die "could not extract strings from $bin"
    mv -f "$out.$$" "$out"
  fi
  echo "$out"
}

# ASSERT a marker discriminates (or deliberately does not), and echo the record
# line. Any disagreement is fatal: a marker table that cannot be reproduced from
# the binaries is exactly the defect this file exists to stop.
# d4_marker_assert <A> <B> <expect_a> <expect_b> <needle>
d4_marker_assert() {
  local A="$1" B="$2" ea="$3" eb="$4" needle="$5"
  local ga gb
  ga="$(d4_marker_state "$A" "$needle")"
  gb="$(d4_marker_state "$B" "$needle")"
  [[ "$ga" == "$ea" && "$gb" == "$eb" ]] \
    || d4_die "marker '$needle' is A=$ga B=$gb, expected A=$ea B=$eb -- it does not establish what this table claims"
  # The needle is QUOTED: one of the real markers ends in a space, and an
  # unquoted record would put trailing whitespace into a committed file.
  printf '# marker        = A=%s B=%s "%s"\n' "$ga" "$gb" "$needle"
}

# A CONTENT identity for the retained corpus inputs actually read: every file
# under every block's `lgdb_raw`, by relative path AND by the sha256 of its
# bytes, in sorted order.
#
# The first version of this hashed paths and SIZES. That is not an identity:
# two fixtures with the same layout and different bytes collide, so a corpus
# could be altered under a table that still "matched". 465 files and 100 MB
# hash in about a second, so there was never a reason to approximate it.
# d4_corpus_identity <corpus-root> <name...>
d4_corpus_identity() {
  local root="$1"; shift
  local m f
  for m in "$@"; do
    printf 'block %s\n' "$m"
    while IFS= read -r f; do
      printf '%s  %s\n' "$(sha256sum "$root/$m/lgdb_raw/$f" | cut -d" " -f1)" "$f"
    done < <(find "$root/$m/lgdb_raw" -type f -printf '%P\n' 2>/dev/null | LC_ALL=C sort)
  done | sha256sum | cut -d' ' -f1
}

# A CONTENT identity for the BASELINE verdicts a classification table quotes.
# Those come from each block's retained `logs/single_edge.log`, which lives
# OUTSIDE `lgdb_raw` and so outside the corpus identity -- meaning a historical
# column could be edited and the table would still reconcile. Measured: review
# turned `debug_breakpoint` from a historical non-refusal into an invented
# refusal and the verifier accepted it.
# d4_baseline_identity <corpus-root> <name...>
d4_baseline_identity() {
  local root="$1"; shift
  local m
  for m in "$@"; do
    printf '%s  %s\n' "$(sha256sum "$root/$m/logs/single_edge.log" 2>/dev/null | cut -d" " -f1)" "$m"
  done | sha256sum | cut -d' ' -f1
}

# Retain a binary this evidence is attributed to, CONTENT-ADDRESSED, and echo
# the retained path. A hash in a header that names a file nobody can produce is
# not authentication; the verifier re-reads this path and re-hashes it.
# `recheck_evidence_binaries.sh` rebuilds either side from its named commit if
# the scratch copy is gone.
# d4_retain_binary <path> -> retained path
d4_retain_binary() {
  local src="$1" sha dir dst
  sha="$(d4_sha "$src")"
  dir="${D4_EVIDENCE_BIN_DIR:-$(git rev-parse --show-toplevel)/temp/d4_evidence_bin}"
  mkdir -p "$dir" || d4_die "cannot create $dir"
  dst="$dir/$sha.lhd"
  if [[ ! -f "$dst" ]]; then
    cp -f "$src" "$dst.$$" || d4_die "cannot retain $src"
    chmod u+w "$dst.$$"
    [[ "$(d4_sha "$dst.$$")" == "$sha" ]] || d4_die "$src changed while being retained"
    mv -f "$dst.$$" "$dst"
  fi
  [[ "$(d4_sha "$dst")" == "$sha" ]] || d4_die "retained $dst does not hash to $sha"
  echo "$dst"
}

# THE SOURCE-TO-BINARY BINDING, required of every binary this evidence names.
#
# Retaining a hash, finding marker strings and copying a commit into a header
# attributes bytes to a commit BY FIAT. Review proved it with a 419-byte shell
# wrapper that carried the expected markers, forwarded every command to the
# other binary, and was published as if it had been built from HEAD.
#
# A binding is written by `d4_build_binding.sh`, which checks the COMPLETE tree
# of an exact commit out into a throwaway worktree, builds it there with its own
# output base, and records what came out. This function refuses to let a binary
# be attributed to a commit unless such a binding exists AND names that exact
# commit, that exact configuration, and that exact binary.
# d4_require_binding <binary> <attestation> <commit> <config> <label>
d4_require_binding() {
  local bin="$1" att="$2" commit="$3" cfg="$4" label="$5"
  [[ -n "$att" ]] || d4_die "$label: no build binding given; a binary with no binding is not attributable to a commit"
  [[ -r "$att" ]] || d4_die "$label: no readable build binding at $att"
  local schema a_commit a_cfg a_sha a_bytes want_sha want_bytes
  schema="$(sed -n 's/^# schema *= *//p' "$att" | head -1)"
  [[ "$schema" == "d4-binary-binding/1" ]] || d4_die "$label: $att is not a d4-binary-binding/1 attestation"
  a_commit="$(sed -n 's/^commit *= *//p' "$att" | head -1)"
  a_cfg="$(sed -n 's/^config *= *//p' "$att" | head -1)"
  a_sha="$(sed -n 's/^sha256 *= *//p' "$att" | head -1)"
  a_bytes="$(sed -n 's/^bytes *= *//p' "$att" | head -1)"
  want_sha="$(d4_sha "$bin")"
  want_bytes="$(stat -c %s "$bin")"
  [[ "$a_commit" == "$commit" ]]     || d4_die "$label: the binding is for commit $a_commit, but this side is named as $commit"
  [[ "$a_cfg" == "$cfg" ]]     || d4_die "$label: the binding was built with [$a_cfg], but this run records [$cfg]"
  [[ "$a_sha" == "$want_sha" ]]     || d4_die "$label: the binding attests $a_sha, but the binary handed in hashes $want_sha -- these bytes were not built from $commit"
  [[ "$a_bytes" == "$want_bytes" ]]     || d4_die "$label: the binding says $a_bytes bytes, the binary is $want_bytes"
}

# Common header fields. Emits to stdout; the caller redirects into the table's
# temporary file and only renames it on success.
# d4_provenance_head <schema> <A> <B> <manifest> <corpus-root> <count> <corpus-id> <config>
d4_provenance_head() {
  local schema="$1" A="$2" B="$3" man="$4" root="$5" count="$6" cid="$7" cfg="$8"
  local Aret Bret
  # NOTHING is published for a binary that is not bound to the commit it is
  # attributed to.
  # B's commit is whatever the producer BUILT, which need not be HEAD.
  : "${B_SOURCE_COMMIT:?B_SOURCE_COMMIT is unset; the producer must name the commit it built}"
  d4_require_binding "$B" "${B_BINDING:-}" "$B_SOURCE_COMMIT" "$cfg" "B"
  Bret="$(d4_retain_binary "$B")"
  echo "# schema          = $schema"
  echo "# source_commit   = $(git rev-parse HEAD)"
  echo "# source_tree     = $(git rev-parse 'HEAD^{tree}')"
  echo "# source_clean    = yes (outside the evidence directory)"
  echo "# bazel_version   = $(bazel --version 2>/dev/null | head -1)"
  echo "# bazel_config    = $cfg"
  echo "# compiler        = $(/usr/bin/gcc-15 --version 2>/dev/null | head -1)"
  echo "# manifest        = $man"
  echo "# manifest_sha256 = $(d4_sha "$man")"
  echo "# manifest_count  = $count"
  echo "# corpus_root     = $root"
  echo "# corpus_identity = $cid"
  if [[ -n "$A" ]]; then
    # The A side was built from a DIFFERENT commit, so `source_commit` above --
    # which is this tree's HEAD -- describes B only. The caller must name A's
    # commit, and the marker assertions below are what substantiate the claim.
    [[ -n "${A_SOURCE_COMMIT:-}" ]] || d4_die "A_SOURCE_COMMIT is unset; a baseline binary with no named source commit is not evidence"
    git rev-parse --verify --quiet "${A_SOURCE_COMMIT}^{commit}" >/dev/null \
      || d4_die "A_SOURCE_COMMIT=$A_SOURCE_COMMIT is not a commit in this repository"
    d4_require_binding "$A" "${A_BINDING:-}" "$(git rev-parse "$A_SOURCE_COMMIT")" "$cfg" "A"
    Aret="$(d4_retain_binary "$A")"
    echo "# A_source_commit = $(git rev-parse "$A_SOURCE_COMMIT")"
    echo "# A_binding       = ${A_BINDING}"
    echo "# A_binding_sha256 = $(d4_sha "${A_BINDING}")"
    echo "# A_path          = $Aret"
    echo "# A_sha256        = $(d4_sha "$Aret")"
    echo "# A_bytes         = $(stat -c %s "$Aret")"
    echo "# A_mtime         = $(stat -c %y "$Aret")"
  fi
  echo "# B_source_commit = $B_SOURCE_COMMIT"
  echo "# B_binding       = ${B_BINDING}"
  echo "# B_binding_sha256 = $(d4_sha "${B_BINDING}")"
  echo "# B_path          = $Bret"
  echo "# B_sha256        = $(d4_sha "$Bret")"
  echo "# B_bytes         = $(stat -c %s "$Bret")"
  echo "# B_mtime         = $(stat -c %y "$Bret")"
}

# ---------------------------------------------------------------------------
# THE BUILD BINDING SCHEMA, shared by `d4_build_binding.sh` and by both
# producers. It lived in the binding script alone, so the producers' own
# `d4_binding_validate` call was `command not found` -- exit 127 on stderr,
# and under `set -uo pipefail` the script simply carried on with an
# unvalidated binding. A schema check only one caller can reach is not one.
# ---------------------------------------------------------------------------
att_field() { sed -n "s/^$2 *= *//p" "$1" | head -1; }

# THE EXACT SCHEMA. A binding is a claim about where bytes came from, so every
# way of writing one that a reader could misinterpret is rejected: a missing
# claim, a duplicated claim (whichever copy wins, the other was never checked),
# an unknown claim, or a malformed value.
# NO `built_at`. A binding must be a pure function of (commit, configuration,
# build result): two producers naming the same commit each rebuild it, and if
# the file carried a timestamp the second would change its bytes and invalidate
# the hash the first had already recorded in its table. Measured exactly that
# way -- the classification probe rebuilt B after the A/B table had recorded
# `B_binding_sha256`, and the verifier rejected the pair. When the binary is
# the same, the binding is now byte-identical, so the order producers run in
# cannot matter. Build time is still visible: the retained artifact has an
# mtime, and each table records `started`/`finished`.
D4_BINDING_KEYS="commit tree config bazel compiler sha256 bytes retained"
d4_binding_validate() {
  local att="$1" k v n
  [[ -r "$att" ]] || d4_die "no readable binding at $att"
  [[ "$(sed -n 's/^# schema *= *//p' "$att" | head -1)" == "d4-binary-binding/1" ]] \
    || d4_die "$att is not a d4-binary-binding/1 attestation"
  # No unknown or duplicated claims.
  while IFS= read -r line; do
    [[ -z "$line" || "$line" == \#* ]] && continue
    k="${line%%=*}"; k="${k// /}"
    grep -qw -- "$k" <<<"$D4_BINDING_KEYS" || d4_die "$att carries an unknown claim '$k'"
  done < "$att"
  for k in $D4_BINDING_KEYS; do
    n="$(grep -cE "^$k[[:space:]]*=" "$att")"
    [[ "$n" -eq 1 ]] || d4_die "$att has $n '$k' claims; exactly one is required"
  done
  v="$(att_field "$att" commit)"; [[ "$v" =~ ^[0-9a-f]{40}$ ]] || d4_die "$att: commit is not a 40-hex id"
  v="$(att_field "$att" tree)";   [[ "$v" =~ ^[0-9a-f]{40}$ ]] || d4_die "$att: tree is not a 40-hex id"
  v="$(att_field "$att" sha256)"; [[ "$v" =~ ^[0-9a-f]{64}$ ]] || d4_die "$att: sha256 is not a 64-hex digest"
  v="$(att_field "$att" bytes)";  [[ "$v" =~ ^[1-9][0-9]*$ ]]  || d4_die "$att: bytes is not a positive integer"
  [[ -n "$(att_field "$att" config)" ]]   || d4_die "$att: config is empty"
  [[ -n "$(att_field "$att" retained)" ]] || d4_die "$att: retained is empty"
  # The tree it names must really be the tree of the commit it names.
  local want
  want="$(git -C "${ROOT:-$PWD}" rev-parse --verify --quiet "$(att_field "$att" commit)^{tree}")" \
    || d4_die "$att: commit $(att_field "$att" commit) is not in this repository"
  [[ "$want" == "$(att_field "$att" tree)" ]] \
    || d4_die "$att: tree $(att_field "$att" tree) is not the tree of its own commit"
}
