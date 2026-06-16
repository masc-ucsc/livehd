#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# --diag-fmt auto|jsonl|pretty: rendering of the stdout result envelope and the
# stderr diagnostic stream. `auto` resolves via isatty(stdout) — tests run with
# stdout piped, so auto == jsonl here; pretty is forced explicitly (and stays
# uncolored off a tty, so the greps below see plain text).

set -u

LHD=lhd/lhd
PRP=inou/prp/tests/equiv/trivial_if.prp
W="${TEST_TMPDIR:-/tmp/lhd_diag_fmt_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

cat >"$W/bad.prp" <<'EOF'
comb f(a:u8) -> (y:u8) {
  y = a + undefined_var
}
EOF

# 1. auto with stdout piped (this script) == jsonl: the bare JSON envelope.
out=$("$LHD" compile "$PRP" --workdir "$W/w1" -q 2>/dev/null)
case "$out" in
  '{"schema_version"'*) ;;
  *) fail "auto+pipe stdout is not the JSON envelope: $out" ;;
esac

# 2. --diag-fmt pretty: a human status line instead of the envelope.
out=$("$LHD" compile "$PRP" --workdir "$W/w2" -q --diag-fmt pretty 2>/dev/null)
echo "$out" | grep -q 'lhd compile pyrope: pass' || fail "pretty pass line missing: $out"
echo "$out" | grep -q '0 errors, 0 warnings' || fail "pretty counts missing: $out"
echo "$out" | grep -q 'schema_version' && fail "pretty leaked the JSON envelope: $out"

# 3. pretty failure: status + error/help block on stdout, clang-style
#    diagnostics on stderr.
"$LHD" compile "$W/bad.prp" --workdir "$W/w3" --diag-fmt pretty >"$W/o3" 2>"$W/e3" && fail "bad.prp must fail"
grep -q 'lhd compile pyrope: fail' "$W/o3" || fail "pretty fail line missing: $(cat "$W/o3")"
grep -q "error\[syntax\]: read of undefined variable" "$W/o3" || fail "pretty error block missing: $(cat "$W/o3")"
grep -q 'help: declare it' "$W/o3" || fail "pretty help line missing: $(cat "$W/o3")"
grep -q 'livehd:.*:error:read of undefined variable' "$W/e3" || fail "pretty stderr must stay clang-style: $(cat "$W/e3")"

# 4. jsonl failure: the stderr diagnostic stream is JSONL records (code, span,
#    hint per line), no clang-style text.
"$LHD" compile "$W/bad.prp" --workdir "$W/w4" --diag-fmt jsonl >"$W/o4" 2>"$W/e4" && fail "bad.prp must fail"
grep -q '"severity":"error".*"message":"read of undefined variable' "$W/e4" || fail "jsonl stderr diag missing: $(cat "$W/e4")"
grep -q '"code":"undefined-read"' "$W/e4" || fail "jsonl diag lost its code: $(cat "$W/e4")"
grep -q '^livehd:' "$W/e4" && fail "jsonl mode leaked clang-style text: $(cat "$W/e4")"

# 5. --result-json always gets the JSON envelope, whatever the display mode.
"$LHD" compile "$PRP" --workdir "$W/w5" -q --diag-fmt pretty --result-json "$W/r5.json" 2>/dev/null \
  || fail "compile with --result-json exited nonzero"
grep -q '"schema_version":1' "$W/r5.json" || fail "--result-json must stay JSON: $(cat "$W/r5.json")"

# 6. A malformed value is a usage error (parse-time, envelope on stdout).
"$LHD" compile "$PRP" --diag-fmt bogus -q 2>/dev/null >"$W/r6.json"
grep -q '"class":"usage"' "$W/r6.json" || fail "--diag-fmt bogus must be a usage error: $(cat "$W/r6.json")"

echo "PASS: --diag-fmt auto|jsonl|pretty"
