#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Clocks and resets bind BY TYPE (docs 04b "Implicit clock and reset",
# 07-typesystem "Clock and Reset"). Compile-only fixtures cannot tell WHICH
# net clocks or resets a register -- a minted `clock` or a swapped pin still
# compiles, and simulation drives whichever clock exists -- so this checks the
# emitted Verilog: every register's `posedge` clock and its reset guard, and
# the nets wired into child instances.

set -u

LHD=./lhd/lhd
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: no lhd binary in $(pwd)"; exit 1; }
W="${TEST_TMPDIR:-/tmp/clkrst_binding_$$}"
mkdir -p "$W"
fail=0

emit() {  # $1=name ; compiles $W/$1.prp into $W/$1_v, prints the Verilog text
  if ! "$LHD" compile "$W/$1.prp" --emit-dir "verilog:$W/$1_v" --workdir "$W/w_$1" >"$W/$1.log" 2>&1; then
    echo "FAIL: compile $1: $(grep '"severity":"error"' "$W/$1.log" | head -2)" >&2
    fail=1
    return
  fi
  cat "$W/$1_v"/*.v
}

# regs <verilog file> -> lines "reg clock reset_guard" per always block
regs() {
  python3 - "$1" <<'EOF'
import re, sys
text = open(sys.argv[1]).read()
for m in re.finditer(r'always @\(posedge (\S+)[^)]*\)\s*begin', text):
    depth, i, body = 1, m.end(), []
    toks = re.finditer(r'\bbegin\b|\bend\b', text[i:])
    end = len(text)
    for t in toks:
        depth += 1 if t.group() == 'begin' else -1
        if depth == 0:
            end = i + t.start()
            break
    blk = text[i:end]
    g = re.search(r'if \(([^)]*)\)', blk)
    for r in sorted(set(re.findall(r'^\s*([A-Za-z_][\w$]*)\s*<=', blk, re.M))):
        print(r, m.group(1), g.group(1).strip() if g else '-')
EOF
}

want() {  # $1=label $2=haystack $3=needle (fixed string)
  if grep -qF -- "$3" <<<"$2"; then echo "ok: $1"; else echo "FAIL: $1: missing '$3' in:"; echo "$2"; fail=1; fi
}
wantnot() {  # $1=label $2=haystack $3=needle
  if grep -qF -- "$3" <<<"$2"; then echo "FAIL: $1: unexpected '$3' in:"; echo "$2"; fail=1; else echo "ok: $1"; fi
}

# A. Any name: the single Clock / Reset input is the implicit pair; nothing is
#    minted and the clk/rst-looking data input stays data.
cat >"$W/any.prp" <<'EOF'
pub mod any(core_clk:Clock, my_rst:Reset, clk:Bool, en:Bool, d:U8) -> (q:U8@[0], p:Bool@[0]) {
  reg r:U8 = 3
  if en { r = d }
  q = r
  p = clk
}
EOF
emit any >"$W/any.v"
R=$(regs "$W/any.v")
want    "any: r clocked by core_clk, reset by my_rst" "$R" "r core_clk my_rst"
wantnot "any: no minted clock"                          "$(cat "$W/any.v")" "input clock"
wantnot "any: no minted reset"                          "$(cat "$W/any.v")" "input reset"

# B. Two Clock inputs: each register's clock_pin, never swapped.
cat >"$W/two_clk.prp" <<'EOF'
pub mod two_clk(clk_a:Clock, clk_b:Clock, rst:Reset, a:U8, b:U8) -> (qa:U8@[0], qb:U8@[0]) {
  reg ra:U8:[clock_pin=clk_a] = 0
  reg rb:U8:[clock_pin=clk_b] = 0
  if a != 0 { ra = a }
  if b != 0 { rb = b }
  qa = ra
  qb = rb
}
EOF
emit two_clk >"$W/two_clk.v"
R=$(regs "$W/two_clk.v")
want "two_clk: ra on clk_a" "$R" "ra clk_a rst"
want "two_clk: rb on clk_b" "$R" "rb clk_b rst"

# C. Two Reset inputs: each register's reset_pin, never swapped.
cat >"$W/two_rst.prp" <<'EOF'
pub mod two_rst(clk:Clock, r1:Reset, r2:Reset, a:U8) -> (qa:U8@[0], qb:U8@[0]) {
  reg ra:U8:[reset_pin=r2] = 1
  reg rb:U8:[reset_pin=r1] = 2
  if a != 0 {
    ra = a
    rb = a
  }
  qa = ra
  qb = rb
}
EOF
emit two_rst >"$W/two_rst.v"
R=$(regs "$W/two_rst.v")
want "two_rst: ra reset by r2" "$R" "ra clk r2"
want "two_rst: rb reset by r1" "$R" "rb clk r1"

# D. A child with no Clock input has one minted; a two-clock caller binds it
#    by name (docs 04b), one instance per clock.
cat >"$W/mint_bind.prp" <<'EOF'
mod child(d:U8, e:Bool) -> (q:U8@[0]) {
  reg x:U8 = 0
  if e { x = d }
  q = x
}
pub mod mint_bind(c1:Clock, c2:Clock, rs:Reset, d:U8, e:Bool) -> (q1:U8@[0], q2:U8@[0]) {
  const a = child(`clock`=c1, d=d, e=e)
  const b = child(`clock`=c2, d=d, e=e)
  q1 = a.q
  q2 = b.q
}
EOF
V=$(emit mint_bind)
want "mint_bind: a clocked by c1" "$V" ".clock(c1)"
want "mint_bind: b clocked by c2" "$V" ".clock(c2)"

# E. An omitted child Reset is wired to the caller's Reset UNCHANGED (a reset
#    is a raw wire; polarity is each register's `negreset=`), exactly like the
#    explicit binding.
cat >"$W/raw_rst.prp" <<'EOF'
mod cnt8(core_clk:Clock, rst_n:Reset, en:Bool) -> (q:U8@[0]) {
  reg cnt:U8:[negreset=true] = 0
  if en { wrap cnt = cnt + 1 }
  q = cnt
}
pub mod raw_rst(clk:Clock, rst:Reset, en:Bool) -> (q:U8@[0], q2:U8@[0]) {
  const c = cnt8(en=en)
  const e = cnt8(rst_n=rst, en=en)
  q  = c.q
  q2 = e.q
}
EOF
V=$(emit raw_rst)
n=$(grep -cF '.rst_n(rst)' <<<"$V")
[ "$n" = "2" ] && echo "ok: raw_rst: both instances get the raw rst" \
  || { echo "FAIL: raw_rst: want 2 '.rst_n(rst)', got $n in:"; echo "$V"; fail=1; }

# F. A backticked type word is an ordinary name, a Clock/Reset one included.
cat >"$W/bt.prp" <<'EOF'
pub mod bt(`U1`:Clock, `Reset`:Reset, d:U8) -> (q:U8@[0]) {
  reg x:U8 = 9
  if d != 0 { x = d }
  q = x
}
EOF
emit bt >"$W/bt.v"
R=$(regs "$W/bt.v")
want    "bt: x on U1, reset by Reset" "$R" "x U1 Reset"
wantnot "bt: no unknown clock"       "$(cat "$W/bt.v")" "cgen-miss"

[ "$fail" -eq 0 ] && echo PASS || exit 1
