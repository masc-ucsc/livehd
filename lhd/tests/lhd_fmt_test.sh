#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd pyrope fmt` — the Pyrope source formatter (prpfmt), a clang-format for
# Pyrope. Covers: stdout default, idempotence, -i in-place rewrite (and its
# no-op on already-formatted input), --indent, a clean parse-error exit, the
# -i/-o conflict guard, and the `lhd pyrope lsp` alias.

set -u

LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_fmt_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# --- messy input, formatted to stdout ---------------------------------------
cat > "$W/messy.prp" <<'EOF'
mod  foo(a:u8,b:u8)->(x:u8@[0],y:u8@[0]){
x=a+b
y=a-b
}
EOF

"$LHD" pyrope fmt "$W/messy.prp" > "$W/out1.prp" 2>"$W/err1" || fail "fmt exited non-zero: $(cat "$W/err1")"
grep -q 'mod foo(a:u8, b:u8) ->' "$W/out1.prp" || fail "header not normalized: $(cat "$W/out1.prp")"
grep -Eq '^    x = a \+ b' "$W/out1.prp" || fail "default indent (4 spaces) missing: $(cat "$W/out1.prp")"

# --- idempotence: formatting the formatted output is a fixed point -----------
"$LHD" pyrope fmt "$W/out1.prp" > "$W/out2.prp" 2>/dev/null || fail "second fmt exited non-zero"
diff "$W/out1.prp" "$W/out2.prp" >/dev/null || fail "formatter is not idempotent"

# --- --indent 2 -------------------------------------------------------------
"$LHD" pyrope fmt "$W/messy.prp" --indent 2 > "$W/i2.prp" 2>/dev/null || fail "fmt --indent 2 exited non-zero"
grep -Eq '^  x = a \+ b' "$W/i2.prp" || fail "--indent 2 did not produce a 2-space indent: $(cat "$W/i2.prp")"
grep -Eq '^    x = a \+ b' "$W/i2.prp" && fail "--indent 2 still emitted a 4-space indent"

# --- -i in place rewrites, and re-running is a no-op -------------------------
cp "$W/messy.prp" "$W/ip.prp"
"$LHD" pyrope fmt -i "$W/ip.prp" 2>/dev/null || fail "fmt -i exited non-zero"
diff "$W/ip.prp" "$W/out1.prp" >/dev/null || fail "-i did not reformat the file to the canonical form"
cp "$W/ip.prp" "$W/ip_before.prp"
"$LHD" pyrope fmt -i "$W/ip.prp" 2>/dev/null || fail "second fmt -i exited non-zero"
diff "$W/ip.prp" "$W/ip_before.prp" >/dev/null || fail "-i changed an already-formatted file"

# --- -o writes to a file -----------------------------------------------------
"$LHD" pyrope fmt "$W/messy.prp" -o "$W/o.prp" 2>/dev/null || fail "fmt -o exited non-zero"
diff "$W/o.prp" "$W/out1.prp" >/dev/null || fail "-o output differs from stdout form"

# --- a non-parsing file fails cleanly (exit 1, no abort) ---------------------
printf 'mod broken(a:u8 ->\n' > "$W/bad.prp"
"$LHD" pyrope fmt "$W/bad.prp" >/dev/null 2>"$W/err_bad"
rc=$?
[ "$rc" -ne 0 ] || fail "fmt of a non-parsing file must exit non-zero"
grep -q 'did not parse' "$W/err_bad" || fail "expected a 'did not parse' diagnostic: $(cat "$W/err_bad")"

# --- argument guards ---------------------------------------------------------
"$LHD" pyrope fmt -i -o "$W/x.prp" "$W/messy.prp" >/dev/null 2>"$W/err_conf"
[ $? -ne 0 ] || fail "-i with -o must be rejected"
grep -q 'mutually exclusive' "$W/err_conf" || fail "expected mutually-exclusive diagnostic"

"$LHD" pyrope fmt >/dev/null 2>"$W/err_none"
[ $? -ne 0 ] || fail "fmt with no files must exit non-zero"
grep -q 'no input files' "$W/err_none" || fail "expected a 'no input files' diagnostic"

# --- `lhd pyrope lsp` starts and exits cleanly on EOF -----------------------
"$LHD" pyrope lsp </dev/null >/dev/null 2>&1 || fail "lhd pyrope lsp did not exit cleanly on EOF"

echo "PASS: lhd pyrope fmt formats, is idempotent, honors -i/-o/--indent, fails cleanly; pyrope lsp alias ok"
