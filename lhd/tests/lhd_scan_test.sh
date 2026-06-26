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

# scan is LEXER-ONLY (no parse/elaborate — it just needs the import strings), so:
#  (a) a file that LEXES but would not PARSE still scans cleanly (imports extracted);
printf 'comb broken(a:u3 -> (z) {\n' > "$W/noparse.prp"
"$LHD" scan "$W/noparse.prp" --workdir "$W/w2" -q --result-json "$W/r2.json" 2>/dev/null \
  || fail "lexer-only scan must NOT fail on a parse-only error: $(cat "$W/r2.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r2.json" || fail "parse-only error should still scan pass: $(cat "$W/r2.json")"

#  (b) a genuine LEX error (unterminated string) fails cleanly with class=syntax.
printf 'const x = import("unterminated\n' > "$W/badlex.prp"
"$LHD" scan "$W/badlex.prp" --workdir "$W/w3" -q --result-json "$W/r3.json" 2>/dev/null
rc=$?
[ $rc -ne 0 ] || fail "scan of an unlexable file must exit non-zero"
grep -q '"class":"syntax"' "$W/r3.json" || fail "expected error.class=syntax: $(cat "$W/r3.json")"

echo "PASS: lexer-only scan reports imports, empty lists, parse-tolerant, fails on lex errors"
