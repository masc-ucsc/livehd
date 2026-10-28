#!/usr/bin/env bash
# FIXTURE-ONLY check of fp_cov_dcert2.sh's IDENTITY binding, REUSE decision and
# attempt immutability.
#
# It extracts the REAL `runner_of`, `identity_of`, `reusable` and `phase` functions out of
# `fp_cov_dcert2.sh` -- so it cannot drift from them -- and exercises them
# against synthetic attempt directories with a stub "lean" that exits 0.  No
# Lean, no certificate, no generation, no simulation, no real benchmark: the
# whole run is well under a second and touches nothing outside its scratch
# directory, which defaults to a PROJECT-LOCAL path.
#
# THE COUNTEREXAMPLES IT EXISTS FOR, all three found by audit:
#   1. a retained cell with `exit` = 0 but GNU time reporting `exit 1` was REUSED
#      as a pass, because the reuse branch read only the `exit` file -- and it
#      wrote "REUSED exit=0" over the retained DISPOSITION.txt, destroying the
#      record it claimed to honour;
#   9. replacing the lean EXECUTABLE's bytes at the same path and version left
#      IDENTITY.txt byte-identical, so a different compiler could be reused as
#      the same one;
#  10. editing a dependency `.olean` at the same LEAN_PATH left IDENTITY.txt
#      byte-identical, so different imported bytes could be reused as the same.
#
# Every case asserts THREE things: the decision, that the retained attempt is
# byte-identical afterwards, and how many new attempt directories appeared.
#
#   fp_cov_dcert2_reuse_test.sh [scratch-dir]
set -uo pipefail
HERE=$(dirname "$(readlink -f "$0")")
SRC=$HERE/../scripts/fp_cov_dcert2.sh
[ -f "$SRC" ] || { echo "no fp_cov_dcert2.sh in ../scripts"; exit 2; }
# PROJECT-LOCAL by default: no temporary directories under the filesystem root.
J=${1:-$HERE/../../../../generated/fixture/fp_cov_reuse_$$}
mkdir -p "$J/lp" || exit 2
J=$(readlink -f "$J")
sed -n '/^runner_of() {/,/^}$/p;/^identity_of() {/,/^}$/p;/^reusable() {/,/^}$/p;/^phase() {/,/^}$/p' "$SRC" > "$J/funcs.sh"
n=$(grep -c '^[a-z_]*() {' "$J/funcs.sh")
[ "$n" = 4 ] || { echo "REFUSING: extracted $n function(s) from $SRC, expected 4"; exit 2; }
echo "extracted the live runner_of/identity_of/reusable/phase from $SRC"
echo "scratch: $J"

printf 'CERTBYTES\n' > "$J/cert.dcert"
printf 'DRIVER\n'    > "$J/driver.lean"
printf 'DEPV1\n'     > "$J/lp/dep.olean"
# The stub stands in for `lean`: it answers `--deps` with the fixture dependency
# so the import_direct binding is exercised, and otherwise exits 0.
cat > "$J/stub-lean" <<SEOS
#!/bin/sh
if [ "\$1" = "--deps" ]; then echo "$J/lp/dep.olean"; exit 0; fi
exit 0
SEOS
chmod +x "$J/stub-lean"

# The harness supplies exactly the globals the real script computes, by the same
# rules, so a mutation in the fixture reaches the identity the same way it would
# in a real run.
cat > "$J/harness.sh" <<'HEOS'
set -uo pipefail
J=$1; PH=$2; FORCE=${3:-0}
LEANBIN=$J/stub-lean; LP="$J/lp"; NATIVE=""; MEMMAX=32G; ROUTE=interpreted
LEANVER=$("$LEANBIN" --version 2>/dev/null || echo "stub 1.0")
LEANSHA=$(sha256sum "$LEANBIN" | cut -d' ' -f1)
LPCLOSURE=$(find "$LP" -name '*.olean' -print0 | sort -z | xargs -0 -r sha256sum | sha256sum | cut -d' ' -f1)
LPNORM="$LP:yes"$'\n'
PABS=$J; TS=FIXTURE
. "$J/funcs.sh"
phase "$PH" 5 "$J/driver.lean" "$J/cert.dcert" 20000 200 >/dev/null 2>&1
HEOS

gen_identity() { ( set -e
  J=$J; LEANBIN=$J/stub-lean; LP="$J/lp"; NATIVE=""; MEMMAX=32G
  LEANVER=$("$LEANBIN" --version 2>/dev/null || echo "stub 1.0")
  LEANSHA=$(sha256sum "$LEANBIN" | cut -d' ' -f1)
  LPCLOSURE=$(find "$LP" -name '*.olean' -print0 | sort -z | xargs -0 -r sha256sum | sha256sum | cut -d' ' -f1)
  LPNORM="$LP:yes"$'\n'
  . "$J/funcs.sh"
  identity_of "$1" 5 "$J/driver.lean" "$J/cert.dcert" "$J/cert.dcert" 20000 200 ); }

gen_identity "$J/good.identity"
gen_identity "$J/other.identity"; sed -i 's/^wall_cap=none memmax=32G$/wall_cap=none memmax=64G/' "$J/other.identity"
cmp -s "$J/good.identity" "$J/other.identity" && { echo "REFUSING: the memmax mutation did not apply"; exit 2; }

fail=0
mkfix() { mkdir -p "$1"; echo "$2" > "$1/exit"
  [ "$3" = "-" ] || printf 'wall 1.0\nexit %s\n' "$3" > "$1/time.txt"
  [ "$4" = "-" ] || echo "$4" > "$1/DISPOSITION.txt"
  echo "original_source_and_input_identity=RETAIN_ME" > "$1/BINDING.txt"
  printf 'RETAINED STDOUT\n' > "$1/stdout.log"
  [ -n "${5:-}" ] && cp "$5" "$1/IDENTITY.txt"
  find "$1" -type f -exec sha256sum {} \; | sort -k2 > "$1.sha.before"; }
check() { local n=$1 lbl=$2 force=$3 wantdec=$4 wantnew=$5; shift 5
  local PH=$J/case$n A=$J/case$n/attempt-0001-OLD; mkdir -p "$PH"
  mkfix "$A" "$@"
  bash "$J/harness.sh" "$J" "$PH" "$force" >/dev/null
  local dec unchanged newdirs ok=PASS
  dec=$(grep ^decision "$PH/decisions/DECISION-FIXTURE.txt" | cut -d= -f2)
  if find "$A" -type f -exec sha256sum {} \; | sort -k2 | diff -q - "$A.sha.before" >/dev/null
  then unchanged=YES; else unchanged=NO; fi
  newdirs=$(ls -d "$PH"/attempt-*FIXTURE 2>/dev/null | wc -l)
  [ "$dec" = "$wantdec" ] && [ "$unchanged" = YES ] && [ "$newdirs" = "$wantnew" ] || { ok=FAIL; fail=1; }
  printf '%-4s %-50s decision=%-6s retained_unchanged=%-4s new_attempts=%s\n' \
         "$ok" "$lbl" "$dec" "$unchanged" "$newdirs"; }

#     n  label                                              force want  new   exit time disposition      identity
check 1 "COUNTEREXAMPLE: exit=0 but producer exit=1"        0 RUN   1    0    1    "OK exit=0"      "$J/good.identity"
check 2 "identity differs (memmax 64G vs 32G)"               0 RUN   1    0    0    "OK exit=0"      "$J/other.identity"
check 3 "no time.txt -- producer status lost"               0 RUN   1    0    -    "OK exit=0"      "$J/good.identity"
check 4 "disposition CAP, not OK"                           0 RUN   1    0    0    "CAP exit=124"   "$J/good.identity"
check 5 "no IDENTITY.txt at all"                            0 RUN   1    0    0    "OK exit=0"
check 6 "retained exit=3 (out of fuel)"                     0 RUN   1    3    3    "REFUSED exit=3" "$J/good.identity"
check 7 "VALID retained attempt"                            0 REUSE 0    0    0    "OK exit=0"      "$J/good.identity"
check 8 "--force over a VALID retained attempt"             1 RUN   1    0    0    "OK exit=0"      "$J/good.identity"

# ---- content-mutation counterexamples ------------------------------------
# 9: the lean EXECUTABLE changes at the SAME path, and `--version` still agrees.
cat > "$J/stub-lean" <<SEOS2
#!/bin/sh
if [ "\$1" = "--deps" ]; then echo "$J/lp/dep.olean"; exit 0; fi
# behaviour changed; path and version did not
exit 0
SEOS2
chmod +x "$J/stub-lean"
check 9 "lean EXECUTABLE bytes changed, same path/version"  0 RUN   1    0    0    "OK exit=0"      "$J/good.identity"

# 10: a DEPENDENCY .olean changes at the SAME LEAN_PATH.
gen_identity "$J/good2.identity"      # identity for the CURRENT binary, pre-dep-change
printf 'DEPV2-MUTATED\n' > "$J/lp/dep.olean"
check 10 "dependency .olean changed at the same LEAN_PATH"  0 RUN   1    0    0    "OK exit=0"      "$J/good2.identity"

# 11: argv is an ARRAY, so one argument containing a space is not two arguments.
gen_identity "$J/argv_a.identity"
( set -e; J=$J; LEANBIN=$J/stub-lean; LP="$J/lp"; NATIVE=""; MEMMAX=32G
  LEANVER=$("$LEANBIN" --version 2>/dev/null || echo "stub 1.0")
  LEANSHA=$(sha256sum "$LEANBIN" | cut -d' ' -f1)
  LPCLOSURE=$(find "$LP" -name '*.olean' -print0 | sort -z | xargs -0 -r sha256sum | sha256sum | cut -d' ' -f1)
  LPNORM="$LP:yes"$'\n'; . "$J/funcs.sh"
  identity_of "$J/argv_b.identity" 5 "$J/driver.lean" "$J/cert.dcert" "$J/cert.dcert" "20000 200" )
if cmp -s "$J/argv_a.identity" "$J/argv_b.identity"; then
  echo "FAIL argv array: ('20000' '200') and ('20000 200') produced the SAME identity"; fail=1
else
  echo "PASS argv array: two arguments and one spaced argument differ in the identity"
fi

echo
if [ "$fail" = 0 ]; then echo "ALL FIXTURE CASES PASS (scratch: $J)"; else echo "FIXTURE FAILURES (scratch: $J)"; fi
exit "$fail"
