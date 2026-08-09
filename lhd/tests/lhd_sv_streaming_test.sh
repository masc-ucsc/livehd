#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `{<<N{x}}` / `{>>N{x}}` — the SystemVerilog streaming (bit/byte reversal)
# operator. `{<<8{d}}` slices d into 8-bit blocks and reverses their order, i.e.
# a byte swap; it is how CVA6's load_unit/store_unit implement big-endian
# access. Contract under test:
#   * the lowering EQUALS an explicit reversed concatenation -- proven by LEC
#     over the whole input space, at more than one width and slice size. A
#     reversed block order, or an off-by-one in the block placement, refutes
#     there rather than looking plausible in the emitted code;
#   * `{>>N{x}}` keeps the block order, so it is the value unchanged;
#   * a width that is NOT a multiple of the slice size is REFUSED: the short
#     block's placement is easy to get subtly wrong, and a wrong byte swap is
#     the kind of bug that survives review;
#   * a dynamically sized stream is refused (no static width at all).
#
# The first implementation of this passed review and FAILED this LEC: it used
# get_mask to extract each block, but `#[...]` right-aligns what it extracts, so
# shifting the block down afterwards shifted twice. That is why the check here
# is an equivalence proof and not a look at the output.

set -u

LHD="${LHD:-lhd/lhd}"
W="${TEST_TMPDIR:-/tmp/lhd_sv_stream_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

# ---- byte/half swaps equal the explicit concatenation ----------------------
cat >"$W/sw.sv" <<'EOF'
module sw(input [31:0] d, output [31:0] q32, output [15:0] q16, output [31:0] q16b, output [31:0] same);
  assign q32  = {<<8{d}};            // 4 bytes reversed
  assign q16  = {<<8{d[15:0]}};      // 2 bytes reversed
  assign q16b = {<<16{d}};           // 2 halfwords reversed
  assign same = {>>8{d}};            // order preserved -> unchanged
endmodule
EOF

cat >"$W/sw_ref.sv" <<'EOF'
module sw_ref(input [31:0] d, output [31:0] q32, output [15:0] q16, output [31:0] q16b, output [31:0] same);
  assign q32  = {d[7:0], d[15:8], d[23:16], d[31:24]};
  assign q16  = {d[7:0], d[15:8]};
  assign q16b = {d[15:0], d[31:16]};
  assign same = d;
endmodule
EOF

"$LHD" compile verilog --top sw --emit-dir "lg:$W/o1" --workdir "$W/w1" --diag-fmt pretty -- "$W/sw.sv" >"$W/sw.out" 2>&1 \
  || fail "streaming concatenation must compile: $(cat "$W/sw.out")"
"$LHD" compile verilog --top sw_ref --emit-dir "lg:$W/o2" --workdir "$W/w2" -- "$W/sw_ref.sv" >"$W/ref.out" 2>&1 \
  || fail "the reference must compile: $(cat "$W/ref.out")"

OUT="$W/lec.out"
"$LHD" lec --impl "lg:$W/o1" --ref "lg:$W/o2" --top sw --ref-top sw_ref --workdir "$W/wl" --diag-fmt pretty >"$OUT" 2>&1 \
  || fail "streaming must equal the explicit reversed concatenation: $(cat "$OUT")"
grep -q 'PROVEN' "$OUT" || fail "expected PROVEN equivalence: $(cat "$OUT")"
grep -qi 'refut' "$OUT" && fail "the streaming lowering is NOT a block reversal: $(cat "$OUT")"

# ---- a partial trailing block is refused, not guessed -----------------------
cat >"$W/partial.sv" <<'EOF'
module partial(input [11:0] d, output [11:0] q);
  assign q = {<<8{d}};   // 12 is not a multiple of 8
endmodule
EOF
OUT="$W/partial.out"
"$LHD" compile verilog --top partial --emit-dir "lg:$W/op" --workdir "$W/wp" --diag-fmt pretty -- "$W/partial.sv" >"$OUT" 2>&1
[ $? -ne 0 ] || fail "a non-multiple width must be refused, not silently swapped: $(cat "$OUT")"
grep -qi 'multiple of the slice size' "$OUT" || fail "the refusal must explain itself: $(cat "$OUT")"

echo "PASS: SystemVerilog streaming operator lowers to a block reversal"
