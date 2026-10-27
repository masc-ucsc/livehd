#!/usr/bin/env bash
# LOAD-BEARING NEGATIVES for the front-end evidence chain.
#
# The producer and the verifier exist to stop a table that looks fine while
# measuring, or authenticating, less than it claims. Twice now a chain that
# looked fine did exactly that -- a hash copied from a configuration-following
# symlink, then a hand-written marker claim that was false of both binaries and
# a verifier that accepted a fabricated one-row table. So each way of getting it
# wrong is reproduced here, and each must FAIL.
#
# Usage: frontend_evidence_controls.sh <ab.tsv> <class.tsv> <A-commit> <B-commit>
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
AB="${1:?usage: frontend_evidence_controls.sh <ab.tsv> <class.tsv> <A-commit> <B-commit>}"
CL="${2:?missing classification table}"
A_COMMIT="${3:?missing A commit}"
B_COMMIT="${4:?missing B commit}"
VERIFY="$HERE/verify_frontend_tables.py"
PRODUCE="$HERE/coreet_frontend_ab.sh"
PY="${PYTHON:-python3}"

cd "$ROOT" || { echo "FATAL: cannot cd $ROOT" >&2; exit 2; }
W="$ROOT/temp/frontend_controls"
rm -rf "$W"; mkdir -p "$W" || { echo "FATAL: cannot create $W" >&2; exit 2; }
fails=0
ok()   { echo "ok: $*"; }
bad()  { echo "CONTROL FAILED: $*"; fails=$((fails+1)); }

# must_fail_with <code> <what> <cmd...>  -- the command must exit with exactly
# <code>. EXACTLY, because the verifier now has two non-zero outcomes: 1 is a
# rejection and 2 is "the tables are fine but the binaries were not
# authenticated". A control that accepted any non-zero status would pass for the
# wrong reason the moment a rebuild was skipped.
must_fail_with() {
  local want="$1" what="$2"; shift 2
  "$@" >"$W/last.log" 2>&1
  local got=$?
  if [ "$got" -eq "$want" ]; then
    ok "$what is rejected"
  else
    echo "--- exit $got, expected $want ---"; tail -6 "$W/last.log"
    bad "$what gave exit $got, expected $want"
  fi
}
# A producer must simply fail; it has no second failure mode.
must_fail() {
  local what="$1"; shift
  if "$@" >"$W/last.log" 2>&1; then
    echo "--- it accepted this, which it must not ---"; tail -6 "$W/last.log"
    bad "$what was ACCEPTED"
  else
    ok "$what is rejected"
  fi
}

# ---- VERIFIER negatives ----------------------------------------------------
# 1. A fabricated one-row table with a name that is not in the manifest. This
#    is the exact shape review fed the previous verifier, which passed it.
{ grep '^#' "$AB"; head -1 <(grep -v '^#' "$AB"); printf 'not_a_real_block\t0\t0\tx\tx\t0123456789abcdef\t0123456789abcdef\tIDENTICAL\n'; } > "$W/ab_onerow.tsv"
must_fail_with 1 "a one-row A/B table naming a block outside the manifest" "$PY" "$VERIFY" --skip-rebuild "$W/ab_onerow.tsv" "$CL"

# 2. Provenance stripped entirely.
grep -v '^#' "$AB" > "$W/ab_noprov.tsv"
must_fail_with 1 "an A/B table with its provenance removed" "$PY" "$VERIFY" --skip-rebuild "$W/ab_noprov.tsv" "$CL"

# 3. Provenance ALTERED. Each of these was ACCEPTED by an earlier revision of
#    the verifier, which is why each one is here.
sed 's/^# B_sha256.*/# B_sha256        = 0000000000000000000000000000000000000000000000000000000000000000/' "$AB" > "$W/ab_badsha.tsv"
cmp -s "$AB" "$W/ab_badsha.tsv" && bad "the altered-hash control did not modify the table"
must_fail_with 1 "an A/B table whose B hash is not the retained binary's" "$PY" "$VERIFY" --skip-rebuild "$W/ab_badsha.tsv" "$CL"

# 3b. The hash left alone but the ARTIFACT swapped for the other side's.
awk -F' = ' 'BEGIN{OFS=" = "} /^# B_path/{ $2=apath } /^# A_path/{ apath=$2 } {print}' "$AB" > "$W/ab_swapbin.tsv"
cmp -s "$AB" "$W/ab_swapbin.tsv" && bad "the artifact-swap control did not modify the table"
must_fail_with 1 "an A/B table whose B_path points at the other binary" "$PY" "$VERIFY" --skip-rebuild "$W/ab_swapbin.tsv" "$CL"

# 3c. A source commit that does not exist.
sed 's/^# source_commit.*/# source_commit   = 0000000000000000000000000000000000000000/' "$AB" > "$W/ab_badcommit.tsv"
must_fail_with 1 "an A/B table naming a source commit that does not exist" "$PY" "$VERIFY" --skip-rebuild "$W/ab_badcommit.tsv" "$CL"

# 3d. A source tree that is not that commit's tree.
sed 's/^# source_tree.*/# source_tree     = 1111111111111111111111111111111111111111/' "$AB" > "$W/ab_badtree.tsv"
must_fail_with 1 "an A/B table whose source_tree is not its commit's tree" "$PY" "$VERIFY" --skip-rebuild "$W/ab_badtree.tsv" "$CL"

# 3e. A count that is not a number. The previous verifier guarded these with
#     `.isdigit()`, so a non-number SKIPPED the check entirely.
sed 's/^# rows .*/# rows           = not-a-number/' "$AB" > "$W/ab_badrows.tsv"
must_fail_with 1 "an A/B table whose row count is not a number" "$PY" "$VERIFY" --skip-rebuild "$W/ab_badrows.tsv" "$CL"

# 3f. A timestamp that is not a timestamp.
sed 's/^# B_mtime.*/# B_mtime         = not-a-time/' "$AB" > "$W/ab_badtime.tsv"
must_fail_with 1 "an A/B table whose binary mtime is not a timestamp" "$PY" "$VERIFY" --skip-rebuild "$W/ab_badtime.tsv" "$CL"

# 3g. A DUPLICATED provenance key: whichever copy wins, a reader quoting the
#     other would be quoting something no tool checked.
awk '/^# rows /{print; print "# rows           = 1"; next} {print}' "$AB" > "$W/ab_dupkey.tsv"
cmp -s "$AB" "$W/ab_dupkey.tsv" && bad "the duplicate-key control did not modify the table"
must_fail_with 1 "an A/B table with a duplicated provenance key" "$PY" "$VERIFY" --skip-rebuild "$W/ab_dupkey.tsv" "$CL"

# 3h. The manifest hash altered.
sed 's/^# manifest_sha256.*/# manifest_sha256 = 0000000000000000000000000000000000000000000000000000000000000000/' "$AB" > "$W/ab_badman.tsv"
must_fail_with 1 "an A/B table whose manifest hash does not match the manifest" "$PY" "$VERIFY" --skip-rebuild "$W/ab_badman.tsv" "$CL"

# 3i. A REFUSED side with no diagnostic code -- a table could otherwise compare
#     two refusals by a code neither of them carries.
awk -F'\t' 'BEGIN{OFS="\t"} /^#/{print;next} $8=="BOTH-REFUSE-SAME"&&!d{$4="-";$5="-";d=1} {print}' "$AB" > "$W/ab_nocode.tsv"
cmp -s "$AB" "$W/ab_nocode.tsv" && bad "the missing-code control did not modify the table"
must_fail_with 1 "a refused A/B row carrying no diagnostic code" "$PY" "$VERIFY" --skip-rebuild "$W/ab_nocode.tsv" "$CL"

# 3j. An EXTRA unusable declaration, with the block's ordinary row left intact.
first_both=$(grep -v '^#' "$AB" | awk -F'\t' '$8=="BOTH-REFUSE-SAME"{print $1; exit}')
[ -n "$first_both" ] || bad "no BOTH-REFUSE-SAME row to borrow for the extra-declaration control"
sed "s/^# input_unusable =\(.*\)/# input_unusable =\1 $first_both/" "$AB" > "$W/ab_extraunusable.tsv"
cmp -s "$AB" "$W/ab_extraunusable.tsv" && bad "the extra-declaration control did not modify the table"
must_fail_with 1 "an input_unusable declaration naming a block whose row says otherwise" "$PY" "$VERIFY" --skip-rebuild "$W/ab_extraunusable.tsv" "$CL"

# 3k. A CORPUS MUTATION of the same size, and an edited BASELINE LOG. Both live
#     in a copy, so the authenticated corpus is never touched.
CORPUS_SRC="$ROOT/temp/coreet_122_auth/blocks"
if [ -d "$CORPUS_SRC" ]; then
  rm -rf "$W/corpus"; cp -a "$CORPUS_SRC" "$W/corpus" 2>/dev/null
  victim=$(find "$W/corpus" -path '*/lgdb_raw/*' -type f -size +16c -print -quit)
  if [ -n "$victim" ]; then
    # flip one byte, keeping the size identical: a size-only identity cannot see this
    printf '\xa5' | dd of="$victim" bs=1 seek=8 count=1 conv=notrunc status=none
    sed "s|^# corpus_root.*|# corpus_root     = $W/corpus|" "$AB" > "$W/ab_corpus.tsv"
    must_fail_with 1 "a same-size corpus mutation" "$PY" "$VERIFY" --skip-rebuild "$W/ab_corpus.tsv" "$CL"
  else
    bad "could not find a corpus file to mutate"
  fi
  vlog=$(grep -v '^#' "$CL" | awk -F'\t' 'NR==2{print $1}')   # NR==1 is the header
  if [ -n "$vlog" ] && [ -f "$W/corpus/$vlog/logs/single_edge.log" ]; then
    printf '\n' >> "$W/corpus/$vlog/logs/single_edge.log"
    sed "s|^# corpus_root.*|# corpus_root     = $W/corpus|" "$CL" > "$W/cl_baseline.tsv"
    must_fail_with 1 "an edited baseline log" "$PY" "$VERIFY" --skip-rebuild "$AB" "$W/cl_baseline.tsv"
  else
    bad "could not find a baseline log to edit"
  fi
else
  bad "no retained corpus to copy for the mutation controls"
fi

# 3l. A BASELINE VERDICT invented in the table while the log says otherwise --
#     the exact probe review used, totals adjusted to match.
awk -F'\t' 'BEGIN{OFS="\t"} /^#/{print;next} $2=="no"&&!d{$2="yes";$3="invented-refusal";$6="CLEARED";d=1} {print}' "$CL" \
  | awk '/^# totals/{sub(/baseline_refusals=[0-9]+/,"baseline_refusals=25"); sub(/cleared=[0-9]+/,"cleared=14"); sub(/still-ok=[0-9]+/,"still-ok=92")} {print}' > "$W/cl_invented.tsv"
cmp -s "$CL" "$W/cl_invented.tsv" && bad "the invented-baseline control did not modify the table"
must_fail_with 1 "an invented historical verdict with the totals adjusted to match" "$PY" "$VERIFY" --skip-rebuild "$AB" "$W/cl_invented.tsv"

# 4. One identity dropped from the body.
{ grep '^#' "$AB"; grep -v '^#' "$AB" | head -1; grep -v '^#' "$AB" | tail -n +3; } > "$W/ab_short.tsv"
must_fail_with 1 "an A/B table with one identity missing" "$PY" "$VERIFY" --skip-rebuild "$W/ab_short.tsv" "$CL"

# 5. A verdict that does not follow from its own row.
awk -F'\t' 'BEGIN{OFS="\t"} /^#/{print;next} $8=="IDENTICAL"&&!d{$8="EMISSION-CHANGED";d=1} {print}' "$AB" > "$W/ab_verdict.tsv"
cmp -s "$AB" "$W/ab_verdict.tsv" && bad "the verdict-flip control did not modify the table"
must_fail_with 1 "an A/B row whose verdict contradicts its own fields" "$PY" "$VERIFY" --skip-rebuild "$W/ab_verdict.tsv" "$CL"

# 6. Totals that do not match the rows.
sed 's/^# totals.*/# totals         = identical=1 both-refused=1 emission-changed=1 acceptance-changed=1 refusal-code-changed=1/' "$AB" > "$W/ab_totals.tsv"
must_fail_with 1 "an A/B table whose printed totals do not follow from its rows" "$PY" "$VERIFY" --skip-rebuild "$W/ab_totals.tsv" "$CL"

# 7. An ACCEPTED side carrying no emission hash -- "both emitted nothing" must
#    not be able to read as IDENTICAL.
awk -F'\t' 'BEGIN{OFS="\t"} /^#/{print;next} $2=="0"&&$3=="0"&&!d{$6="-";$7="-";d=1} {print}' "$AB" > "$W/ab_nov.tsv"
cmp -s "$AB" "$W/ab_nov.tsv" && bad "the empty-emission control did not modify the table"
must_fail_with 1 "an accepted A/B row with no emission hash" "$PY" "$VERIFY" --skip-rebuild "$W/ab_nov.tsv" "$CL"

# 8. The classification table losing an identity that is neither a row nor
#    declared skipped.
{ grep '^#' "$CL"; grep -v '^#' "$CL" | head -1; grep -v '^#' "$CL" | tail -n +3; } > "$W/cl_short.tsv"
must_fail_with 1 "a classification table with an identity that is neither a row nor skipped" "$PY" "$VERIFY" --skip-rebuild "$AB" "$W/cl_short.tsv"

# 9. Its provenance stripped.
grep -v '^#' "$CL" > "$W/cl_noprov.tsv"
must_fail_with 1 "a classification table with its provenance removed" "$PY" "$VERIFY" --skip-rebuild "$AB" "$W/cl_noprov.tsv"

# ---- PRODUCER negatives ----------------------------------------------------
# The producer now takes COMMITS and builds both sides itself, so the channels
# the old controls attacked -- a caller-supplied executable, a caller-supplied
# attestation -- do not exist to attack. What is left is what it still accepts
# from a caller, and each of those is tried here. (Its cgen-exit and
# empty-emission guards remain in the code as defence in depth; they are no
# longer reachable from outside, because the only binaries it ever runs are the
# ones it built, so they are exercised at the verifier instead, by a row that
# claims an acceptance with no emission hash.)
MAN="$HERE/coreet_122.list"

# 10. A manifest naming a block with no retained input.
{ cat "$MAN"; echo "a_block_that_was_never_retained"; } > "$W/man_extra.list"
must_fail "a manifest naming a block with no retained input" \
  env MANIFEST="$W/man_extra.list" "$PRODUCE" "$A_COMMIT" "$B_COMMIT" "$W/out1.tsv"
[ -e "$W/out1.tsv" ] && bad "the producer published a table despite aborting"

# 11. A manifest that omits a block the corpus retains.
head -n -1 "$MAN" > "$W/man_short.list"
must_fail "a manifest that omits a retained block" \
  env MANIFEST="$W/man_short.list" "$PRODUCE" "$A_COMMIT" "$B_COMMIT" "$W/out2.tsv"
[ -e "$W/out2.tsv" ] && bad "the producer published a table despite aborting"

# 12. The same commit on both sides: the table would compare nothing.
must_fail "an A/B run naming the same commit on both sides" \
  "$PRODUCE" "$B_COMMIT" "$B_COMMIT" "$W/out3.tsv"
[ -e "$W/out3.tsv" ] && bad "the producer published a table despite aborting"

# 13. A commit that does not exist.
must_fail "an A/B run naming a commit that does not exist" \
  "$PRODUCE" 0000000000000000000000000000000000000000 "$B_COMMIT" "$W/out4.tsv"
[ -e "$W/out4.tsv" ] && bad "the producer published a table despite aborting"

# 14. THE CLOSED CHANNEL. A path to an executable where a commit is expected --
#     the shape review's forwarding wrapper arrived through. It is not a commit,
#     so it never reaches a build, let alone a measurement.
cat > "$W/lhd_wrapper" <<'WEOF'
#!/usr/bin/env bash
WEOF
printf 'exec %q "$@"\n' "/bin/true" >> "$W/lhd_wrapper"
chmod +x "$W/lhd_wrapper"
must_fail "a path to an executable where a commit is expected" \
  "$PRODUCE" "$A_COMMIT" "$W/lhd_wrapper" "$W/out5.tsv"
[ -e "$W/out5.tsv" ] && bad "the producer published a table for a non-commit"

# 14b. THE SCHEMA CHECK MUST BE REACHABLE FROM THE SCRIPT THAT CALLS IT.
#      `d4_binding_validate` lived in `d4_build_binding.sh`, so both producers'
#      calls to it were `command not found`: exit 127 on stderr, and under
#      `set -uo pipefail` the producer carried straight on with an unvalidated
#      binding. A schema check only one caller can reach is not one, so this
#      asserts BOTH that the shared library defines it and that every script
#      that calls it gets it from there.
for s_ in coreet_frontend_ab.sh coreet_frontend_refusal_probe.sh d4_build_binding.sh; do
  grep -q 'd4_binding_validate' "$HERE/$s_" || continue
  if bash -c ". '$HERE/d4_evidence_provenance.sh' >/dev/null 2>&1; declare -F d4_binding_validate >/dev/null"; then
    ok "$s_ can reach d4_binding_validate through the shared provenance library"
  else
    bad "$s_ calls d4_binding_validate, but sourcing the shared library does not define it"
  fi
done
# ...and it REJECTS, rather than merely existing. A duplicated claim: whichever
# copy wins, the other was never checked.
GENUINE="$(sed -n 's/^# B_binding  *= *//p' "$AB" | head -1)"
if [[ -r "$GENUINE" ]]; then
  { cat "$GENUINE"; sed -n 's/^\(sha256 *=.*\)$/\1/p' "$GENUINE"; } > "$W/dup.binding"
  cmp -s "$GENUINE" "$W/dup.binding" && bad "the duplicated-claim control did not modify the binding"
  must_fail "a binding carrying the same claim twice" "$HERE/d4_build_binding.sh" check "$W/dup.binding"
else
  bad "the A/B table names a B binding that is not readable, so the schema control cannot run"
fi

# 15. A HAND-WRITTEN BINDING FOR A FORWARDING WRAPPER -- review's attack, in the
#     only place it can still be mounted: a table that already carries it.
#     Every claim is internally consistent (the right commit, that commit's real
#     tree, the recorded configuration, and the wrapper's own hash and size), so
#     nothing but rebuilding the named tree can tell it apart. This control
#     therefore runs the verifier WITH the rebuild, and it must exit 1.
WRAP="$W/lhd_forward"
{ echo '#!/usr/bin/env bash'; printf 'exec %q "$@"\n' "$(sed -n 's/^# B_path *= *//p' "$AB" | head -1)"; } > "$WRAP"
chmod +x "$WRAP"
wsha="$(sha256sum "$WRAP" | cut -d' ' -f1)"
wbytes="$(stat -c %s "$WRAP")"
bcommit="$(sed -n 's/^# B_source_commit *= *//p' "$AB" | head -1)"
bcfg="$(sed -n 's/^# bazel_config *= *//p' "$AB" | head -1)"
{
  echo "# schema   = d4-binary-binding/1"
  echo "commit     = $bcommit"
  echo "tree       = $(git -C "$ROOT" rev-parse "$bcommit^{tree}")"
  echo "config     = $bcfg"
  echo "bazel      = bazel 9.2.0"
  echo "compiler   = gcc-15"
  echo "sha256     = $wsha"
  echo "bytes      = $wbytes"
  echo "retained   = $WRAP"
} > "$W/forged.binding"
sed -e "s|^# B_binding  *=.*|# B_binding       = $W/forged.binding|" \
    -e "s|^# B_binding_sha256  *=.*|# B_binding_sha256 = $(sha256sum "$W/forged.binding" | cut -d' ' -f1)|" \
    -e "s|^# B_path  *=.*|# B_path          = $WRAP|" \
    -e "s|^# B_sha256  *=.*|# B_sha256        = $wsha|" \
    -e "s|^# B_bytes  *=.*|# B_bytes         = $wbytes|" "$AB" > "$W/ab_forged.tsv"
cmp -s "$AB" "$W/ab_forged.tsv" && bad "the forged-binding control did not modify the table"
# It must be internally consistent, or it would be caught by shape alone and
# would prove nothing about the rebuild.
if "$PY" "$VERIFY" --skip-rebuild "$W/ab_forged.tsv" "$CL" >"$W/forged_shape.log" 2>&1; then
  bad "the forged table was expected to pass the SHAPE checks (so the rebuild is what catches it)"
else
  if [ "$(grep -c '^FAIL' "$W/forged_shape.log")" -eq 0 ]; then
    ok "the forged binding is internally consistent: only a rebuild can tell"
  else
    echo "--- shape failures (the forgery was sloppy, so this control is weak) ---"
    grep '^FAIL' "$W/forged_shape.log" | head -3
    bad "the forged binding was caught by shape, not by the rebuild"
  fi
fi
must_fail_with 1 "a hand-written binding for a marker-bearing forwarding wrapper" \
  "$PY" "$VERIFY" "$W/ab_forged.tsv" "$CL"

# ---- and the real tables must still verify, REBUILD INCLUDED --------------
# Not `--skip-rebuild`: the one positive in this file has to exercise the whole
# acceptance path, or the rebuild would be a step nothing ever runs.
if "$PY" "$VERIFY" "$AB" "$CL" >"$W/real.log" 2>&1; then
  ok "the real tables verify, and both binaries rebuild from their named commits"
else
  tail -10 "$W/real.log"; bad "the real tables do NOT verify"
fi

echo
if [ "$fails" -eq 0 ]; then echo "PASS: frontend_evidence_controls"; else echo "FAIL: $fails control(s)"; fi
exit $(( fails > 0 ))
