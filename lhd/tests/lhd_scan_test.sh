#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd scan` — pyrope import/dependency discovery for build systems: each
# file's import strings ride the result envelope's "scan" member (raw, as
# written; path resolution lands with the import.md resolver).

set -u

LHD=lhd/lhd
FIX=lhd/tests/scan_imports.prp
PLAIN=inou/prp/tests/equiv/trivial_if.prp
W="${TEST_TMPDIR:-/tmp/lhd_scan_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" scan "$FIX" "$PLAIN" --workdir "$W/w" -q --result-json "$W/r.json" || fail "scan exited non-zero: $(cat "$W/r.json" 2>/dev/null)"

grep -q '"status":"pass"' "$W/r.json" || fail "result not pass: $(cat "$W/r.json")"
grep -q '"imports":\["some/bar","xxx/zzz/yyy.foo"\]' "$W/r.json" || fail "missing expected imports: $(cat "$W/r.json")"
grep -q '"imports":\[\]' "$W/r.json" || fail "import-free file should report an empty list: $(cat "$W/r.json")"

# A file that does not parse must fail cleanly (syntax class), not pass.
printf 'comb broken(a:u3 -> (z) {\n' > "$W/bad.prp"
"$LHD" scan "$W/bad.prp" --workdir "$W/w2" -q --result-json "$W/r2.json" 2>/dev/null
rc=$?
[ $rc -ne 0 ] || fail "scan of a non-parsing file must exit non-zero"
grep -q '"class":"syntax"' "$W/r2.json" || fail "expected error.class=syntax: $(cat "$W/r2.json")"

echo "PASS: scan reports imports, empty lists, and clean syntax failures"
