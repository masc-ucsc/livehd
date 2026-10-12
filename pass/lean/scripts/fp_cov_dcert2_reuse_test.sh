#!/usr/bin/env bash
# FIXTURE-ONLY check of fp_cov_dcert2.sh's REUSE decision and attempt immutability.
#
# It extracts the REAL `identity_of`, `reusable` and `phase` functions out of
# `fp_cov_dcert2.sh` -- so it cannot drift from them -- and exercises them
# against synthetic attempt directories with a stub "lean" that exits 0.  No
# Lean, no certificate, no generation, no simulation: the whole run is under a
# second and touches nothing outside its scratch directory.
#
# THE COUNTEREXAMPLE IT EXISTS FOR.  A retained cell with `exit` = 0 but GNU time
# reporting `exit 1` was REUSED as a pass, because the reuse branch read only the
# `exit` file and skipped the status validation a fresh phase performs -- and it
# wrote "REUSED exit=0" over the retained `DISPOSITION.txt`, destroying the
# record it claimed to be honouring.  Case 1 below is exactly that fixture.
#
# Every case asserts THREE things: the decision, that the retained attempt is
# byte-identical afterwards, and how many new attempt directories appeared.
#
#   fp_cov_dcert2_reuse_test.sh [scratch-dir]
set -uo pipefail
SRC=$(dirname "$(readlink -f "$0")")/fp_cov_dcert2.sh
[ -f "$SRC" ] || { echo "no fp_cov_dcert2.sh beside this test"; exit 2; }
J=${1:-$(mktemp -d)}
mkdir -p "$J/lp"
sed -n '/^identity_of() {/,/^}$/p;/^reusable() {/,/^}$/p;/^phase() {/,/^}$/p' "$SRC" > "$J/funcs.sh"
n=$(grep -c '^[a-z_]*() {' "$J/funcs.sh")
[ "$n" = 3 ] || { echo "REFUSING: extracted $n function(s) from $SRC, expected 3"; exit 2; }
echo "extracted the live identity_of/reusable/phase from $SRC"

printf 'CERTBYTES\n' > "$J/cert.dcert"
printf 'DRIVER\n'    > "$J/driver.lean"
printf '#!/bin/sh\nexit 0\n' > "$J/stub-lean"; chmod +x "$J/stub-lean"

cat > "$J/harness.sh" <<'HEOS'
set -uo pipefail
J=$1; PH=$2; FORCE=${3:-0}
LEANBIN=$J/stub-lean; LEANVER="stub 1.0"; LP="$J/lp"
LPSHA=$(printf '%s' "$LP" | sha256sum | cut -d' ' -f1); PABS=$J; TS=FIXTURE
. "$J/funcs.sh"
phase "$PH" 5 "$J/driver.lean" "$J/cert.dcert" 20000 200 >/dev/null 2>&1
HEOS

gen_identity() { ( set -e; . "$J/funcs.sh"
  LEANBIN=$J/stub-lean; LEANVER="stub 1.0"; LP="$J/lp"
  LPSHA=$(printf '%s' "$LP" | sha256sum | cut -d' ' -f1)
  identity_of "$1" 5 "$J/driver.lean" "$J/cert.dcert" "$J/cert.dcert" 20000 200 ); }
gen_identity "$J/good.identity"
gen_identity "$J/other.identity"; sed -i 's/^timeout_s=5$/timeout_s=600/' "$J/other.identity"

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
  printf '%-4s %-48s decision=%-6s retained_unchanged=%-4s new_attempts=%s\n' \
         "$ok" "$lbl" "$dec" "$unchanged" "$newdirs"; }

#     n  label                                            force want  new   exit time disposition      identity
check 1 "THE COUNTEREXAMPLE: exit=0 but producer exit=1"  0 RUN   1    0    1    "OK exit=0"      "$J/good.identity"
check 2 "identity differs (timeout 600 vs 5)"             0 RUN   1    0    0    "OK exit=0"      "$J/other.identity"
check 3 "no time.txt -- producer status lost"             0 RUN   1    0    -    "OK exit=0"      "$J/good.identity"
check 4 "disposition CAP, not OK"                         0 RUN   1    0    0    "CAP exit=124"   "$J/good.identity"
check 5 "no IDENTITY.txt at all"                          0 RUN   1    0    0    "OK exit=0"
check 6 "retained exit=3 (out of fuel)"                   0 RUN   1    3    3    "REFUSED exit=3" "$J/good.identity"
check 7 "VALID retained attempt"                          0 REUSE 0    0    0    "OK exit=0"      "$J/good.identity"
check 8 "--force over a VALID retained attempt"           1 RUN   1    0    0    "OK exit=0"      "$J/good.identity"

echo
if [ "$fail" = 0 ]; then echo "ALL 8 FIXTURE CASES PASS (scratch: $J)"; else echo "FIXTURE FAILURES (scratch: $J)"; fi
exit "$fail"
