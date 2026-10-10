#!/usr/bin/env bash
# LOAD-BEARING NEGATIVES for the fanout inventory PRODUCER.
#
# These drive `coreet_emission_fanout.sh` ITSELF, not a copy of its logic. The
# producer's embedded reader used to ignore the analyzer's exit status, print
# `UNPARSED` or `TOOL-FAILED` into the row, carry on, and reach the end with
# status 0 -- and it ran `mv` BEFORE checking whether anything had failed, so a
# run that did return nonzero still left the requested table on disk. A control
# that re-implemented the result handling would have proved nothing about
# either, so the analyzer is addressed through `D4_FANOUT_TOOL` and a STUB is
# put in its place.
#
# Usage: coreet_emission_fanout_controls.sh [A-commit] [before-commit] [after-commit]
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
PROD="$HERE/coreet_emission_fanout.sh"
A="${1:-ce5fdfc7a}"
BEFORE="${2:-391b1eb90}"
AFTER="${3:-84b06c6df}"
CORPUS="${CORPUS:-$ROOT/temp/coreet_122_auth/blocks}"
MOD="${D4_FANOUT_CTRL_MODULE:-vpu_tensora_rf}"

cd "$ROOT" || { echo "FATAL: cannot cd $ROOT" >&2; exit 2; }
[[ -r "$PROD" ]] || { echo "FATAL: no producer at $PROD" >&2; exit 2; }
[[ -d "$CORPUS/$MOD/lgdb_raw" ]] || { echo "FATAL: no retained corpus for $MOD" >&2; exit 2; }

W="$ROOT/temp/fanout_controls"
rm -rf "$W"; mkdir -p "$W" || { echo "FATAL: cannot create $W" >&2; exit 2; }
fails=0
ok()  { echo "ok: $*"; }
bad() { echo "CONTROL FAILED: $*"; fails=$((fails+1)); }

printf '%s\n' "$MOD" > "$W/one.list"

# ---- THE PUBLICATION BOUNDARY ---------------------------------------------
# "Nonzero implies no $OUT" cannot be shown dynamically for the post-rename
# region, because the correction's whole point is that the region holds nothing
# that can fail. So it is checked STRUCTURALLY, which is also what makes
# reverting the correction visible: the previous shape renamed first and
# validated after, so a short read exited nonzero with the requested path
# populated.
# `BOUNDARY_*`, not `AFTER`: `$AFTER` is this script's AFTER-COMMIT argument, and
# reusing the name clobbered it -- every later control then ran the producer
# with the boundary text where a commit belonged and refused for the wrong
# reason. Caught by the needles those controls carry.
BOUNDARY_TAIL="$(awk '/^mv -f "\$TMP" "\$OUT"/{f=1;next} f' "$PROD" | grep -vE '^[[:space:]]*(#|$)')"
BOUNDARY_EXPECT=$'echo "wrote $OUT"\nexit 0'
if [[ "$BOUNDARY_TAIL" == "$BOUNDARY_EXPECT" ]]; then
  ok "nothing that can fail the invocation follows the publication rename"
else
  echo "    after the rename: $(tr '\n' '|' <<<"$BOUNDARY_TAIL")"
  bad "an operation that can fail the invocation follows the publication rename"
fi

# ...and the rename is reached only after the CANDIDATE has been validated.
BOUNDARY_PRE="$(awk '/^# ---- VALIDATE THE CANDIDATE/{f=1} /^mv -f "\$TMP" "\$OUT"/{exit} f' "$PROD")"
pre_ok=1
for needle in 'schema' 'producer_sha256' 'the candidate table holds'; do
  grep -qF "$needle" <<<"$BOUNDARY_PRE" \
    || { bad "the candidate is not checked for '$needle' before the rename"; pre_ok=0; }
done
if grep -qF '"$OUT"' <<<"$BOUNDARY_PRE"; then
  bad "a pre-rename assertion names \$OUT, which does not exist yet"
  pre_ok=0
fi
[[ "$pre_ok" -eq 1 ]] && ok "every schema, hash and row assertion is made on the candidate before the rename"

# The boundary is a SOURCE-level property, so it can be checked without running
# anything. `D4_FANOUT_CTRL_STRUCTURAL_ONLY=1` stops here, which is how
# reverting the correction is demonstrated without a half-hour of emissions --
# this is the committed control itself, not a copy of its logic.
if [[ -n "${D4_FANOUT_CTRL_STRUCTURAL_ONLY:-}" ]]; then
  echo
  if [[ "$fails" -eq 0 ]]; then echo "PASS: coreet_emission_fanout_controls (structural only)";
  else echo "FAIL: $fails control(s)"; fi
  exit $(( fails > 0 ))
fi


# stub <name> <body>  -- a stand-in analyzer
stub() { printf '#!/usr/bin/env python3\n%s\n' "$2" > "$W/$1.py"; chmod +x "$W/$1.py"; }

# must_refuse <what> <needle> <tool> [list]
#
# The producer must exit nonzero, leave the requested output ABSENT, and say
# WHY. The needle is not decoration: the row path and the publish gate are
# independent guards that both end in "nonzero and no table", so a control that
# only checked the outcome passed with EITHER of them reverted. Pinning the
# diagnostic is what makes each guard falsifiable on its own.
must_refuse() {
  local what="$1" needle="$2" tool="$3" list="${4:-$W/one.list}" out="$W/out.tsv" rc
  rm -f "$out"
  if [[ "$tool" == "-" ]]; then
    "$PROD" "$A" "$BEFORE" "$AFTER" "$list" "$out" >"$W/last.log" 2>&1
  else
    D4_FANOUT_TOOL="$tool" "$PROD" "$A" "$BEFORE" "$AFTER" "$list" "$out" >"$W/last.log" 2>&1
  fi
  rc=$?
  if [[ "$rc" -eq 0 ]]; then
    tail -4 "$W/last.log"; bad "$what: the producer exited 0"; return
  fi
  if [[ -e "$out" ]]; then
    bad "$what: the producer exited $rc but PUBLISHED $out anyway"; return
  fi
  if ! grep -qF "$needle" "$W/last.log"; then
    tail -4 "$W/last.log"; bad "$what: refused, but not with '$needle'"; return
  fi
  ok "$what is refused by its own guard (exit $rc, no table, '$needle')"
}

stub nonzero  'import sys; sys.stderr.write("stub failure\n"); sys.exit(9)'
must_refuse "an analyzer that exits nonzero" "the analyzer exited 9" "$W/nonzero.py"

stub garbage  'print("this is not json")'
must_refuse "an analyzer that prints no readable JSON" "printed no readable JSON" "$W/garbage.py"

stub unparsed 'import json; print(json.dumps({"unparsed": 3, "verdict": "UNPARSED"}))'
must_refuse "an analyzer that could not parse the netlist" "could not parse" "$W/unparsed.py"

stub wrongseed 'import json,sys; print(json.dumps({"unparsed":0,"seeds":{"not_the_seed":["STATE"]}}))'
must_refuse "an analyzer that answers about a seed it was not asked about" "was asked about" "$W/wrongseed.py"

stub extraseed 'import json,sys; d={s:["STATE"] for s in sys.argv[2:]}; d["bonus"]=["STATE"]; print(json.dumps({"unparsed":0,"seeds":d}))'
must_refuse "an analyzer that answers about an EXTRA seed" "was asked about" "$W/extraseed.py"

stub unknowncls 'import json,sys; print(json.dumps({"unparsed":0,"seeds":{s:["WAT"] for s in sys.argv[2:]}}))'
must_refuse "an analyzer that reports an unknown class" "unknown class" "$W/unknowncls.py"

stub emptycls 'import json,sys; print(json.dumps({"unparsed":0,"seeds":{s:[] for s in sys.argv[2:]}}))'
must_refuse "an analyzer that reports an empty class set" "empty class set" "$W/emptycls.py"

printf '%s\n' "a_module_that_is_not_in_the_corpus" > "$W/bogus.list"
must_refuse "a module list naming a block with no retained input" "produced no netlist" "-" "$W/bogus.list"

# A DUPLICATE IDENTITY. Every row here is produced successfully, so this is the
# one control whose refusal comes from the PUBLISH GATE rather than from the row
# path -- which is what makes "publish only after everything reconciles"
# falsifiable. With the `mv` moved back before the checks, this control sees the
# table on disk.
printf '%s\n%s\n' "$MOD" "$MOD" > "$W/dup.list"
must_refuse "a module list naming the same block twice" "unique identities" "-" "$W/dup.list"

# The same commit on both compared sides: the table would compare nothing.
rm -f "$W/out.tsv"
if "$PROD" "$A" "$AFTER" "$AFTER" "$W/one.list" "$W/out.tsv" >"$W/last.log" 2>&1; then
  bad "before and after naming the same commit was accepted"
elif [[ -e "$W/out.tsv" ]]; then
  bad "before and after naming the same commit published a table anyway"
else
  ok "before and after naming the same commit is refused (no table published)"
fi

# ---- THE PUBLICATION CONTRACT --------------------------------------------
# Deliberately NOT using `must_refuse`: that helper removes the destination
# first, and removing it is exactly what hid this. A failed rerun must not leave
# the PREVIOUS run's table at the path the caller asked this invocation to
# produce.
STALE="$W/stale.tsv"
printf '%b\n' "# schema          = d4-emission-fanout/1" \
               "# STALE SENTINEL -- the previous run's table" \
               "module	changed_before	fanout_before	changed_after	fanout_after" \
               "a_block_from_last_time	1	STATE	1	STATE" > "$STALE"
if D4_FANOUT_TOOL="$W/nonzero.py" "$PROD" "$A" "$BEFORE" "$AFTER" "$W/one.list" "$STALE" \
     >"$W/stale.log" 2>&1; then
  bad "a failing run with a pre-existing table exited 0"
elif [[ -e "$STALE" ]]; then
  echo "    still there: $(head -2 "$STALE" | tr '\n' ' ')"
  bad "a failing run left the PREVIOUS table at the requested path"
else
  ok "a failing run leaves no stale table at the requested path"
fi

# A destination that cannot hold a table at all. The old epilogue moved the
# temporary file INSIDE the directory, failed its `tail`, and still printed
# `wrote` and exited 0.
DESTDIR="$W/dest_is_a_dir"
rm -rf "$DESTDIR"; mkdir -p "$DESTDIR"
if "$PROD" "$A" "$BEFORE" "$AFTER" "$W/one.list" "$DESTDIR" >"$W/destdir.log" 2>&1; then
  bad "a directory destination was accepted"
elif grep -q "^wrote " "$W/destdir.log"; then
  bad "a directory destination still produced a 'wrote' claim"
elif [[ -n "$(find "$DESTDIR" -mindepth 1 -print -quit 2>/dev/null)" ]]; then
  echo "    deposited: $(find "$DESTDIR" -mindepth 1 | head -3 | tr '\n' ' ')"
  bad "a candidate file was deposited inside the directory destination"
elif ! grep -q "is not a regular file" "$W/destdir.log"; then
  # THE DESTINATION CONTRACT, by name. Without this the control passed with the
  # whole contract reverted, because the `rm -f` that clears a previous table
  # also fails on a directory -- a second guard reaching the same outcome.
  tail -2 "$W/destdir.log"
  bad "a directory destination was refused, but not by the destination contract"
else
  ok "a non-publishable destination is refused by the destination contract (no 'wrote', nothing deposited)"
fi

# ---- and the real analyzer still produces a table -------------------------
rm -f "$W/out.tsv"
if "$PROD" "$A" "$BEFORE" "$AFTER" "$W/one.list" "$W/out.tsv" >"$W/real.log" 2>&1; then
  n="$(awk -F'\t' '$1!~/^#/ && $1!="module" && NF==5' "$W/out.tsv" | wc -l)"
  id="$(awk -F'\t' '$1!~/^#/ && $1!="module" && NF==5 {print $1}' "$W/out.tsv")"
  want_prod="$(sha256sum "$PROD" | cut -d' ' -f1)"
  got_prod="$(sed -n 's/^# producer_sha256 = //p' "$W/out.tsv" | head -1)"
  if [[ ! -f "$W/out.tsv" || -L "$W/out.tsv" ]]; then
    bad "the positive succeeded but $W/out.tsv is not a regular table"
  elif ! grep -q "^wrote " "$W/real.log"; then
    bad "the positive succeeded without the 'wrote' claim"
  elif [[ "$n" -ne 1 ]]; then
    bad "the positive produced $n five-field rows, not 1"
  elif [[ "$id" != "$MOD" ]]; then
    bad "the positive produced a row for '$id', not for $MOD"
  elif [[ "$got_prod" != "$want_prod" ]]; then
    bad "the table records producer_sha256 $got_prod, but the producer hashes to $want_prod"
  else
    ok "the real analyzer produces exactly one five-field row for $MOD, stamped with this producer's hash"
  fi
else
  tail -5 "$W/real.log"; bad "the positive control did not produce a table"
fi

echo
if [[ "$fails" -eq 0 ]]; then echo "PASS: coreet_emission_fanout_controls"; else echo "FAIL: $fails control(s)"; fi
exit $(( fails > 0 ))
