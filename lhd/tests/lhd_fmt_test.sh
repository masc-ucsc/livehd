#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `lhd pyrope fmt` — the Pyrope source formatter (prpfmt), a clang-format for
# Pyrope. Covers: stdout default, idempotence, -i in-place rewrite (and its
# no-op on already-formatted input), --indent, the two layouts (--mode ai, the
# default: no width limit, no alignment; --mode human: --width wrapping and
# alignment), the --width-in-ai-mode warning, a clean parse-error exit, the
# argument guards, and the `lhd pyrope lsp` alias.

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
mod  foo(a:U8,b:U8)->(x:U8@[0],y:U8@[0]){
x=a+b
y=a-b
}
EOF

"$LHD" pyrope fmt "$W/messy.prp" > "$W/out1.prp" 2>"$W/err1" || fail "fmt exited non-zero: $(cat "$W/err1")"
grep -q 'mod foo(a:U8, b:U8) ->' "$W/out1.prp" || fail "header not normalized: $(cat "$W/out1.prp")"
grep -Eq '^  x = a \+ b' "$W/out1.prp" || fail "default indent (2 spaces) missing: $(cat "$W/out1.prp")"

# --- idempotence: formatting the formatted output is a fixed point -----------
"$LHD" pyrope fmt "$W/out1.prp" > "$W/out2.prp" 2>/dev/null || fail "second fmt exited non-zero"
diff "$W/out1.prp" "$W/out2.prp" >/dev/null || fail "formatter is not idempotent"

# --- --indent N: a NON-DEFAULT value must reach the formatter ----------------
# The default is 2, so probing with `--indent 2` would only restate the default
# check above -- it would still pass with the value dropped on the floor.
"$LHD" pyrope fmt "$W/messy.prp" --indent 4 > "$W/i4.prp" 2>/dev/null || fail "fmt --indent 4 exited non-zero"
grep -Eq '^    x = a \+ b' "$W/i4.prp" || fail "--indent 4 did not produce a 4-space indent: $(cat "$W/i4.prp")"
# safe as a negative: a 4-space line has a space in column 3, so `^  x` cannot match it
grep -Eq '^  x = a \+ b' "$W/i4.prp" && fail "--indent 4 still emitted the default 2-space indent"

# --- --width N (--mode human): the value must reach the formatter too --------
# Compared by INEQUALITY against the default so the check does not depend on
# prpfmt's exact wrap text.
cat > "$W/wide.prp" <<'EOF'
mod wide(first_input:U8,second_input:U8,third_input:U8,fourth_input:U8)->(result:U8@[0]){
result=first_input+second_input+third_input+fourth_input
}
EOF
"$LHD" pyrope fmt "$W/wide.prp" --mode human > "$W/w_def.prp" 2>/dev/null || fail "fmt --mode human (default width) exited non-zero"
"$LHD" pyrope fmt "$W/wide.prp" --mode human --width 40 > "$W/w40.prp" 2>/dev/null || fail "fmt --mode human --width 40 exited non-zero"
diff "$W/w_def.prp" "$W/w40.prp" >/dev/null && fail "--mode human --width 40 matched the default 132 output (value not plumbed)"

# --- --mode ai (the default): no width limit, a warning for --width ---------
# The AI layout ignores the width, so --width there is a mistake worth a
# warning, and the output must be byte-identical to the width-less run (and to
# an explicit --mode ai).
"$LHD" pyrope fmt "$W/wide.prp" > "$W/ai_def.prp" 2>"$W/ai_def.err" || fail "fmt (ai default) exited non-zero"
grep -q 'width-ignored' "$W/ai_def.err" && fail "ai mode warned about --width although none was given"
"$LHD" pyrope fmt "$W/wide.prp" --width 40 > "$W/ai40.prp" 2>"$W/ai40.err" || fail "fmt --width 40 (ai) exited non-zero"
grep -q 'width-ignored' "$W/ai40.err" || fail "--width in ai mode did not warn: $(cat "$W/ai40.err")"
diff -u "$W/ai_def.prp" "$W/ai40.prp" || fail "--width changed the ai-mode output"
"$LHD" pyrope fmt "$W/wide.prp" --mode ai > "$W/ai_explicit.prp" 2>/dev/null || fail "fmt --mode ai exited non-zero"
diff -u "$W/ai_def.prp" "$W/ai_explicit.prp" || fail "--mode ai differs from the default layout"
diff "$W/ai_def.prp" "$W/w40.prp" >/dev/null && fail "ai mode wrapped like --mode human --width 40"

# AI mode keeps same-kind consecutive assignments unaligned; human aligns them.
cat > "$W/assigns.prp" <<'EOF'
a = 1
long_name = 2
EOF
cat > "$W/assigns_human_expected.prp" <<'EOF'
a         = 1
long_name = 2
EOF
"$LHD" pyrope fmt "$W/assigns.prp" --verify > "$W/assigns_ai.prp" 2>/dev/null || fail "ai assigns fmt failed"
diff -u "$W/assigns.prp" "$W/assigns_ai.prp" || fail "ai mode vertically aligned assignments"
"$LHD" pyrope fmt "$W/assigns.prp" --mode human --verify > "$W/assigns_human.prp" 2>/dev/null || fail "human assigns fmt failed"
diff -u "$W/assigns_human_expected.prp" "$W/assigns_human.prp" || fail "human mode did not align assignments"

# --- long if chains break at each branch and keep short bodies inline -------
# (--mode human; a plain assignment and a `const` are different statement
# kinds, so their `=` do not align)
cat > "$W/chain.prp" <<'EOF'
alu_out_0 = unique if instr.beq { alu_eq } elif instr.bne { not alu_eq } elif instr.bge { not alu_lts } elif instr.bgeu { not alu_ltu } elif instr.is_slti_blt_slt { alu_lts } elif instr.is_sltiu_bltu_sltu { alu_ltu } else { 0ub? != 0 }
const short = if a { 1 } elif b { 2 } else { 3 }
EOF
cp "$W/chain.prp" "$W/chain_ai.prp"
cat > "$W/chain_expected.prp" <<'EOF'
alu_out_0 = unique if instr.beq { alu_eq }
  elif instr.bne { not alu_eq }
  elif instr.bge { not alu_lts }
  elif instr.bgeu { not alu_ltu }
  elif instr.is_slti_blt_slt { alu_lts }
  elif instr.is_sltiu_bltu_sltu { alu_ltu }
  else { 0ub? != 0 }
const short = if a { 1 } elif b { 2 } else { 3 }
EOF
"$LHD" pyrope fmt -i "$W/chain.prp" --mode human --verify 2>"$W/chain.err" || fail "chain fmt failed: $(cat "$W/chain.err")"
diff -u "$W/chain_expected.prp" "$W/chain.prp" || fail "if chain did not wrap at branch boundaries"
"$LHD" pyrope fmt -i "$W/chain.prp" --mode human --verify 2>"$W/chain.err" || fail "second chain fmt failed"
diff -u "$W/chain_expected.prp" "$W/chain.prp" || fail "if chain formatting is not idempotent"
# ai mode: no width limit, so the 225-column chain is already in canonical form
cp "$W/chain_ai.prp" "$W/chain_ai_before.prp"
"$LHD" pyrope fmt -i "$W/chain_ai.prp" --verify 2>"$W/chain_ai.err" || fail "ai chain fmt failed: $(cat "$W/chain_ai.err")"
diff -u "$W/chain_ai_before.prp" "$W/chain_ai.prp" || fail "ai mode wrapped a long if chain"

# The width is a soft limit: keep dotted names intact, even on narrow lines.
cat > "$W/identifier.prp" <<'EOF'
const x = instr.slli
const y = instr.very_long_member_name.another_very_long_member_name
EOF
"$LHD" pyrope fmt "$W/identifier.prp" --mode human --width 16 --verify >"$W/identifier_out.prp" 2>"$W/identifier.err" || fail "identifier fmt failed"
diff -u "$W/identifier.prp" "$W/identifier_out.prp" || fail "formatter broke a dotted identifier"

# Repeated operand shapes align their selectors and operators with assignment.
cat > "$W/aligned.prp" <<'EOF'
comb f() -> () {
  const compressed_load_offset = (mem_rdata_latched#[5] << 6) | (mem_rdata_latched#[10..=12] << 3) | (mem_rdata_latched#[6] << 2)
}
EOF
cat > "$W/aligned_expected.prp" <<'EOF'
comb f() -> () {
  const compressed_load_offset = (mem_rdata_latched#[      5] << 6)
                               | (mem_rdata_latched#[10..=12] << 3)
                               | (mem_rdata_latched#[      6] << 2)
}
EOF
"$LHD" pyrope fmt -i "$W/aligned.prp" --mode human --width 100 --verify 2>"$W/aligned.err" || fail "aligned fmt failed"
diff -u "$W/aligned_expected.prp" "$W/aligned.prp" || fail "repeated operands did not align"
"$LHD" pyrope fmt -i "$W/aligned.prp" --mode human --width 100 --verify 2>"$W/aligned.err" || fail "second aligned fmt failed"
diff -u "$W/aligned_expected.prp" "$W/aligned.prp" || fail "operand alignment is not idempotent"

# Exercise operand-vector growth in the executable that links both the real
# tree-sitter nodes and the compiler's AST facade. In debug builds their old
# global TSNode names caused incompatible vector methods to collide at link time.
printf 'const x = data[0]' > "$W/many_operands.prp"
printf 'const x = data[0]\n' > "$W/many_operands_expected.prp"
for index in 1 2 3 4 5 6 7 8; do
  printf ' | data[%s]' "$index" >> "$W/many_operands.prp"
  printf '        | data[%s]\n' "$index" >> "$W/many_operands_expected.prp"
done
printf '\n' >> "$W/many_operands.prp"
"$LHD" pyrope fmt -i "$W/many_operands.prp" --mode human --width 40 --verify 2>"$W/many_operands.err" || fail "operand-vector fmt failed"
diff -u "$W/many_operands_expected.prp" "$W/many_operands.prp" || fail "operand-vector growth corrupted nodes"

# Other expressions use one indentation level for leading operator continuations.
cat > "$W/continuation.prp" <<'EOF'
comb f() -> () {
  const result = first_long_operand + second_long_operand + third_long_operand
}
EOF
cat > "$W/continuation_expected.prp" <<'EOF'
comb f() -> () {
  const result = first_long_operand
    + second_long_operand
    + third_long_operand
}
EOF
"$LHD" pyrope fmt "$W/continuation.prp" --mode human --width 40 --verify >"$W/continuation_out.prp" 2>"$W/continuation.err" || fail "continuation fmt failed"
diff -u "$W/continuation_expected.prp" "$W/continuation_out.prp" || fail "operator continuation indent is not one level"

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
printf 'mod broken(a:U8 ->\n' > "$W/bad.prp"
"$LHD" pyrope fmt "$W/bad.prp" >/dev/null 2>"$W/err_bad"
rc=$?
[ "$rc" -ne 0 ] || fail "fmt of a non-parsing file must exit non-zero"
grep -q 'did not parse' "$W/err_bad" || fail "expected a 'did not parse' diagnostic: $(cat "$W/err_bad")"

# --- argument guards ---------------------------------------------------------
"$LHD" pyrope fmt -i -o "$W/x.prp" "$W/messy.prp" >/dev/null 2>"$W/err_conf"
[ $? -ne 0 ] || fail "-i with -o must be rejected"
grep -q 'mutually exclusive' "$W/err_conf" || fail "expected mutually-exclusive diagnostic"

for opt in --indent --width; do
  for value in 0 -1 2x 2147483648; do
    "$LHD" pyrope fmt "$W/messy.prp" "$opt" "$value" >"$W/err_number" 2>&1
    [ $? -ne 0 ] || fail "$opt accepted invalid value $value"
    grep -q 'positive integer' "$W/err_number" || fail "missing integer diagnostic"
  done
done

for value in bogus AI ""; do
  "$LHD" pyrope fmt "$W/messy.prp" --mode "$value" >"$W/err_mode" 2>&1
  [ $? -ne 0 ] || fail "--mode accepted invalid value '$value'"
  grep -q 'expects ai or human' "$W/err_mode" || fail "missing --mode diagnostic: $(cat "$W/err_mode")"
done

"$LHD" pyrope fmt >/dev/null 2>"$W/err_none"
[ $? -ne 0 ] || fail "fmt with no files must exit non-zero"
grep -q 'no input files' "$W/err_none" || fail "expected a 'no input files' diagnostic"

# --- `lhd pyrope lsp` starts and exits cleanly on EOF -----------------------
"$LHD" pyrope lsp </dev/null >/dev/null 2>&1 || fail "lhd pyrope lsp did not exit cleanly on EOF"

echo "PASS: lhd pyrope fmt formats, is idempotent, honors -i/-o/--indent/--mode/--width, fails cleanly; pyrope lsp alias ok"
