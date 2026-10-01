#!/bin/bash
# Nested block comments and `x` == x in places the fixture runner cannot reach:
#   1. the incremental compile cache fingerprint (stable_source_fingerprint)
#      must end a block comment at its MATCHING `*/` -- two programs that only
#      differ inside/outside a nested comment must not share a netlist -- and
#      (1b) must never strip string or backtick-name bytes as a comment;
#   2. the parser-depth guard must not be switched off by a `"` inside a
#      nested comment, or by a `//` inside a string or backtick name (the
#      deep expression then crashed the parser);
#   3. test/formal block names are canonical: `cnt`.`basic` == cnt.basic,
#      for the listed name and for the selector, in either spelling.
set -euo pipefail

LHD=./bazel-bin/lhd/lhd
if [[ ! -x "$LHD" ]]; then
  LHD=./lhd/lhd
fi

WD="${TEST_TMPDIR:-tmp_prp_comment_backtick_smoke}/comment_backtick_smoke"
rm -rf "$WD"
mkdir -p "$WD"
fail() {
  echo "FAIL: $*"
  exit 1
}

# --- 1. incremental fingerprint ------------------------------------------------
# A means o = p (the `+ 1` is inside the nested comment); B means o = p + 1.
cat >"$WD/A.prp" <<'EOF'
pub comb fp(p:U8) -> (o:U9) {
  o = p /* /* */ + 1 */
}
EOF
cat >"$WD/B.prp" <<'EOF'
pub comb fp(p:U8) -> (o:U9) {
  o = p + 1 /* /* */ */
}
EOF
mkdir -p "$WD/inc/wd"
cp "$WD/A.prp" "$WD/inc/fp.prp"
"$LHD" compile "$WD/inc/fp.prp" --workdir "$WD/inc/wd" --set lhd.incremental=true \
  --emit verilog:"$WD/warmA.v" -q >/dev/null
cp "$WD/B.prp" "$WD/inc/fp.prp"
"$LHD" compile "$WD/inc/fp.prp" --workdir "$WD/inc/wd" --set lhd.incremental=true \
  --emit verilog:"$WD/warmB.v" -q >/dev/null
"$LHD" compile "$WD/inc/fp.prp" --emit verilog:"$WD/coldB.v" -q >/dev/null
grep -q 'o = p;' "$WD/warmA.v" || fail "A (o = p) did not compile to o = p"
diff <(grep 'o = ' "$WD/warmB.v") <(grep 'o = ' "$WD/coldB.v") >/dev/null \
  || fail "warm B reused A's netlist: $(grep 'o = ' "$WD/warmB.v")"
grep -q 'o = p;' "$WD/warmB.v" && fail "warm B compiled to A's o = p"

# --- 1b. fingerprint vs comment-like bytes inside strings ----------------------
# Each pair differs ONLY in string bytes that look like a comment: a `//` or
# `/*` after `'\'` (single-quoted strings have no escapes) or inside a string
# nested in a `{…}` hole. The fingerprint must see them as string bytes, so the
# warm B compile must match a cold B compile (not reuse A's netlist).
fp_pair() {  # $1 name, $2 A source, $3 B source
  local d="$WD/fp_$1"
  mkdir -p "$d/wd"
  printf '%s\n' "$2" >"$d/t.prp"
  "$LHD" compile "$d/t.prp" --workdir "$d/wd" --set lhd.incremental=true --emit verilog:"$d/warmA.v" -q >/dev/null
  printf '%s\n' "$3" >"$d/t.prp"
  "$LHD" compile "$d/t.prp" --workdir "$d/wd" --set lhd.incremental=true --emit verilog:"$d/warmB.v" -q >/dev/null
  "$LHD" compile "$d/t.prp" --emit verilog:"$d/coldB.v" -q >/dev/null
  diff <(grep ' = ' "$d/warmB.v") <(grep ' = ' "$d/coldB.v") >/dev/null \
    || fail "$1: warm B reused A's netlist"
  diff <(grep ' = ' "$d/warmA.v") <(grep ' = ' "$d/warmB.v") >/dev/null \
    && fail "$1: A and B compiled alike (the pair does not test the fingerprint)"
  return 0
}
fp_pair sq_backslash \
'pub comb top(a:U8) -> (r:U8) {
  const b = '"'"'\'"'"'
  const s = "x'"'"'//2"
  r = if s == "x'"'"'//1" and b != '"'"'z'"'"' { a } else { 0 }
}' \
'pub comb top(a:U8) -> (r:U8) {
  const b = '"'"'\'"'"'
  const s = "x'"'"'//1"
  r = if s == "x'"'"'//1" and b != '"'"'z'"'"' { a } else { 0 }
}'
fp_pair hole_line \
'pub comb top(a:U8) -> (r:U8) {
  const s = "{"a" ++ "//2"}"
  r = if s == '"'"'a//1'"'"' { a } else { 0 }
}' \
'pub comb top(a:U8) -> (r:U8) {
  const s = "{"a" ++ "//1"}"
  r = if s == '"'"'a//1'"'"' { a } else { 0 }
}'
fp_pair hole_block \
'pub comb top(a:U8) -> (r:U8) {
  const s = "{"a" ++ "/*2*/"}"
  r = if s == '"'"'a/*1*/'"'"' { a } else { 0 }
}' \
'pub comb top(a:U8) -> (r:U8) {
  const s = "{"a" ++ "/*1*/"}"
  r = if s == '"'"'a/*1*/'"'"' { a } else { 0 }
}'
fp_pair backtick_name \
'pub comb top(a:U8) -> (r:U8) {
  const `x//y` = 1
  r = a ^ `x//y`
}' \
'pub comb top(a:U8) -> (r:U8) {
  const `x//y` = 2
  r = a ^ `x//y`
}'

# --- 2. depth guard vs a quote inside a nested comment --------------------------
python3 - "$WD/deep.prp" <<'EOF'
import sys
n = 20000
with open(sys.argv[1], 'w') as f:
    f.write('/* /* */ " */\n')
    f.write('pub comb deep(a:U8) -> (r:U8) {\n')
    f.write('  r = ' + '(' * n + 'a' + ')' * n + '\n')
    f.write('}\n')
EOF
set +e
out=$("$LHD" compile "$WD/deep.prp" --diag-fmt pretty 2>&1)
rc=$?
set -e
[[ $rc -ne 0 && $rc -lt 128 ]] || fail "deep nesting after a nested comment: rc=$rc (crash or accepted)"
grep -q 'nesting too deep' <<<"$out" || fail "deep nesting: no nesting-too-deep diagnostic: $out"
# ... nor by a `//` inside a single-quoted string, a string nested in a hole or
# a backtick name on the same line as the deep expression.
python3 - "$WD/deep2.prp" <<'EOF'
import sys
n = 20000
with open(sys.argv[1], 'w') as f:
    f.write('pub comb deep(a:U8) -> (r:U8) {\n')
    f.write("  const `x//y` = 1; const s = '//'; const t = \"{\"//\"}\"; r = " + '(' * n + 'a' + ')' * n + '\n')
    f.write('}\n')
EOF
set +e
out=$("$LHD" compile "$WD/deep2.prp" --diag-fmt pretty 2>&1)
rc=$?
set -e
[[ $rc -ne 0 && $rc -lt 128 ]] || fail "deep nesting after '//' in a string: rc=$rc (crash or accepted)"
grep -q 'nesting too deep' <<<"$out" || fail "deep nesting after '//' in a string: no nesting-too-deep diagnostic: $out"

# --- 3. canonical test / formal block names ------------------------------------
cat >"$WD/s.prp" <<'EOF'
mod `cnt`(`en`:Bool) -> (`value`:U8@[0]) {
  reg `count`:U8 = 0
  `value` = `count`
  if `en` { wrap `count` += 1 }
}

test `cnt`.`basic` {
  mut `acc` = `cnt`
  `acc`.`en` = true
  step
}

formal `cnt`.`bounded` {
  mut `acc` = `cnt`
  `assume_nocheck`(`acc`.`en` == false)
  `assert`(`acc`.`count` != 5, "frozen")
}
EOF
out=$("$LHD" sim "$WD/s.prp" --list-tests --diag-fmt pretty 2>&1) || fail "sim --list-tests: $out"
grep -qx '  cnt.basic' <<<"$out" || fail "test name not canonical: $out"
for sel in 'cnt.basic' '`cnt`.`basic`' '`cnt`.basic'; do
  "$LHD" sim "$WD/s.prp" "$sel" --list-tests --diag-fmt pretty >/dev/null 2>&1 \
    || fail "sim selector '$sel' did not match"
done
for sel in 'cnt.bounded' '`cnt`.`bounded`'; do
  out=$("$LHD" formal verify "$WD/s.prp" --top cnt --formal "$sel" --list-tests --diag-fmt pretty 2>&1) \
    || fail "formal selector '$sel' did not match: $out"
  grep -q 'cnt.bounded' <<<"$out" || fail "formal list for '$sel': $out"
  grep -q '`cnt`' <<<"$out" && fail "formal block name not canonical: $out"
done

echo "comment_backtick_smoke: PASS"
