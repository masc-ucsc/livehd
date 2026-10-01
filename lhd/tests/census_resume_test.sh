#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The fail-closed --resume gate of scripts/coreet_d2_census.sh.
#
# WHY RESUME EXISTS. Generation runs under `xargs -P`. If the top-level driver
# dies, xargs is reparented to PPID 1 and its workers run to completion with
# nobody left to aggregate or sweep -- which is exactly what happened: a kill
# intended for stray children took the driver itself, and 121 of 122 modules
# had finished generating. Rerunning all of them to recover is pure waste.
#
# WHY IT MUST FAIL CLOSED. A resumed census that silently aggregates a partial
# or foreign generation directory is worse than no census, because it looks
# authoritative. Every check below is a refusal, not a warning.
#
# Exercises only the gate: no lhd, no Lean, no CORE-ET.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRV=""
for c in "${TEST_SRCDIR:-}/_main/scripts/coreet_d2_census.sh" \
         "$(cd "$HERE/../.." 2>/dev/null && pwd)/scripts/coreet_d2_census.sh" \
         "scripts/coreet_d2_census.sh"; do
  [ -r "$c" ] && { DRV="$c"; break; }
done
[ -n "$DRV" ] || { echo "FAIL: cannot find coreet_d2_census.sh"; exit 1; }

T="${TEST_TMPDIR:-$(cd "$HERE/../.." && pwd)/generated/tests}/census_resume"
rm -rf "$T"; mkdir -p "$T"
fails=0

# A 3-module toy corpus, byte-sorted, with the digest the driver demands.
printf 'aaa\nbbb\nccc\n' > "$T/mods.raw"
DIG="$(head -c -1 "$T/mods.raw" | sha256sum | cut -d' ' -f1)"
{ echo "# toy corpus"; echo "#   $DIG"; cat "$T/mods.raw"; } > "$T/corpus.txt"
# Computed EXACTLY as the driver computes it, including the failure mode: in a
# bazel sandbox there is no git repo and both sides get the empty string, which
# must still compare equal. A fallback value here would make the test's
# manifest disagree with the driver and fail for the wrong reason.
COMMIT="$(git -C "$(dirname "$DRV")/.." rev-parse --short HEAD 2>/dev/null)"

RUNNER_H="$(sha256sum "$(dirname "$DRV")/run_coreet_module_lean.sh" 2>/dev/null | cut -d' ' -f1)"
LHD_H="$(sha256sum "$(dirname "$DRV")/../bazel-bin/lhd/lhd" 2>/dev/null | cut -d' ' -f1)"

mkrun() {  # <dir> <n-status> [extra-module]
  local d="$1" n="$2" extra="${3:-}"
  rm -rf "$d"; mkdir -p "$d/mod" "$d/logs"
  cat > "$d/manifest.json" <<JSON
{"commit": "$COMMIT", "module_list_sha256": "$DIG",
 "runner_sha256": "$RUNNER_H", "lhd_sha256": "$LHD_H"}
JSON
  local i=0
  for m in aaa bbb ccc; do
    i=$((i+1)); [ "$i" -gt "$n" ] && break
    mkdir -p "$d/mod/$m"; printf 'ifx\t0\n' > "$d/mod/$m/status"
  done
  [ -n "$extra" ] && { mkdir -p "$d/mod/$extra"; printf 'ifx\t0\n' > "$d/mod/$extra/status"; }
  return 0
}
run_resume() {  # <dir>
  LIST="$T/corpus.txt" EXPECT_N=3 SKIP_SWEEP=1 \
    bash "$DRV" --resume "$1" --out "$1" 2>&1
}
# A refusal must leave the run directory BYTE FOR BYTE unchanged. The gate
# once ran AFTER the manifest was rewritten, so a refused resume had already
# damaged the evidence it existed to protect -- and a missing manifest was
# quietly created as `{}`.
expect_refusal() {  # <desc> <dir> <needle>
  local before=""
  [ -f "$2/manifest.json" ] && before="$(sha256sum "$2/manifest.json" | cut -d' ' -f1)"
  local out; out="$(run_resume "$2")"; local rc=$?
  local after=""
  [ -f "$2/manifest.json" ] && after="$(sha256sum "$2/manifest.json" | cut -d' ' -f1)"
  if [ "$before" != "$after" ]; then
    echo "FAIL: $1 -- a REFUSED resume modified manifest.json"; fails=$((fails+1))
  fi
  if [ -z "$before" ] && [ -n "$after" ]; then
    echo "FAIL: $1 -- a refused resume CREATED a manifest that did not exist"; fails=$((fails+1))
  fi
  if [ "$rc" -eq 0 ]; then
    echo "FAIL: $1 -- resume returned success"; fails=$((fails+1)); return
  fi
  if ! echo "$out" | grep -qi "$3"; then
    echo "FAIL: $1 -- refused, but not for the stated reason (wanted /$3/)"
    echo "$out" | sed 's/^/      /' | head -4; fails=$((fails+1)); return
  fi
  echo "ok: $1 refused"
}

echo "--- an incomplete generation must be refused, not aggregated ---"
mkrun "$T/partial" 2
expect_refusal "2 of 3 status files" "$T/partial" "status file"

echo "--- artifacts from a different corpus must be refused ---"
mkrun "$T/otherlist" 3
sed -i "s/$DIG/$(printf 'f%.0s' {1..64})/" "$T/otherlist/manifest.json"
expect_refusal "module-list digest mismatch" "$T/otherlist" "digest"

echo "--- artifacts generated at a different commit must be refused ---"
mkrun "$T/othercommit" 3
sed -i "s/\"commit\": \"$COMMIT\"/\"commit\": \"0000000\"/" "$T/othercommit/manifest.json"
expect_refusal "commit mismatch" "$T/othercommit" "commit"

echo "--- resuming across a commit needs an EXPLICIT, recorded reason ---"
mkrun "$T/across" 3
sed -i "s/\"commit\": \"$COMMIT\"/\"commit\": \"0000000\"/" "$T/across/manifest.json"
out="$(LIST="$T/corpus.txt" EXPECT_N=3 SKIP_SWEEP=1 bash "$DRV" --resume "$T/across" \
        --out "$T/across" --resume-across-commit "report-only change" 2>&1)"
if [ -z "$LHD_H" ] || [ -z "$RUNNER_H" ]; then
  # No lhd/runner to hash (a bazel sandbox has neither). Then it CANNOT be
  # shown that the generation-critical inputs are unchanged, and the override
  # must be refused -- a free-text reason is not evidence.
  if echo "$out" | grep -q "cannot be shown that"; then
    echo "ok: without hashable generation inputs, --resume-across-commit is refused"
  else
    echo "FAIL: unverifiable generation inputs did not refuse the override"
    echo "$out" | sed 's/^/      /' | head -4; fails=$((fails+1))
  fi
else
  if echo "$out" | grep -q "resume: 3/3 status files"; then
    echo "ok: an explicit --resume-across-commit is allowed when inputs hash-match"
  else
    echo "FAIL: --resume-across-commit did not permit the resume"
    echo "$out" | sed 's/^/      /' | head -4; fails=$((fails+1))
  fi
  grep -q "report-only change" "$T/across/manifest.json" \
    && echo "ok: the stated reason is recorded in the manifest" \
    || { echo "FAIL: the override reason was not recorded"; fails=$((fails+1)); }
fi

echo "--- a stray module directory outside the corpus must be refused ---"
mkrun "$T/stray" 3 zzz_not_in_corpus
expect_refusal "module dir not in the corpus" "$T/stray" "not in the corpus"

echo "--- no manifest at all must be refused, with an exact diagnostic ---"
mkrun "$T/nomani" 3; rm -f "$T/nomani/manifest.json"
expect_refusal "missing manifest" "$T/nomani" "manifest.json is missing or unreadable"
[ -f "$T/nomani/manifest.json" ] \
  && { echo "FAIL: the refused resume created manifest.json"; fails=$((fails+1)); } \
  || echo "ok: a missing manifest is not silently created"

echo "--- resuming across a commit requires generation inputs to be unchanged ---"
mkrun "$T/badhash" 3
sed -i "s/\"commit\": \"$COMMIT\"/\"commit\": \"0000000\"/" "$T/badhash/manifest.json"
sed -i "s/\"runner_sha256\": \"[0-9a-f]*\"/\"runner_sha256\": \"$(printf 'a%.0s' {1..64})\"/" "$T/badhash/manifest.json"
expect_refusal "changed module runner" "$T/badhash" "module runner changed"

echo "--- --resume-across-commit without --resume is rejected ---"
out="$(LIST="$T/corpus.txt" EXPECT_N=3 SKIP_SWEEP=1 bash "$DRV" --resume-across-commit why \
        --out "$T/noresume" 2>&1)"; rc=$?
[ "$rc" -ne 0 ] && echo "$out" | grep -q "no meaning without --resume" \
  && echo "ok: --resume-across-commit alone is rejected" \
  || { echo "FAIL: --resume-across-commit without --resume was accepted"; fails=$((fails+1)); }

echo "--- a complete, matching directory must be ACCEPTED ---"
mkrun "$T/good" 3
out="$(run_resume "$T/good")"; rc=$?
echo "$out" | sed 's/^/      /' | head -6
if echo "$out" | grep -q "resume: 3/3 status files"; then
  echo "ok: a complete matching generation resumes"
else
  echo "FAIL: a complete, matching generation was refused (rc=$rc)"; fails=$((fails+1))
fi
# and it must not clobber the generation manifest
grep -q '"resumed"' "$T/good/manifest.json" \
  && echo "ok: resume is recorded without overwriting the generation manifest" \
  || { echo "FAIL: resume did not record itself in the manifest"; fails=$((fails+1)); }
grep -q "$DIG" "$T/good/manifest.json" \
  || { echo "FAIL: resume overwrote the original generation provenance"; fails=$((fails+1)); }
# the successful record must carry the AGGREGATION-side provenance too
for k in aggregated_at_commit generation_input_hash_check aggregation_driver_sha256; do
  grep -q "$k" "$T/good/manifest.json" \
    || { echo "FAIL: the resume record is missing $k"; fails=$((fails+1)); }
done
echo "ok: the resume record carries aggregation-side provenance"

[ "$fails" -eq 0 ] || { echo "FAIL: $fails case(s) failed"; exit 1; }
echo "PASS: census_resume_test"
