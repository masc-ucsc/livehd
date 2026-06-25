#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Regression for the slang reader lowering a scalar packed-struct VARIABLE as a
# per-field BUNDLE (one independent net per field) instead of a single flat
# packed bus + bit-slices.
#
# The dual-issue ALU has an `io` packed struct whose '{...} assignment computes a
# field (`result`) from reads of its OTHER fields (`operation`/`inputx`). As one
# flat `io` node those reads are bit-slices of the SAME net the assignment
# writes, so `result`'s driver transitively reads `io` itself — a FALSE
# combinational self-loop. The cvc5 LEC/formal encoder cannot encode it
# ("operand of get_mask has no encodable driver (combinational cycle?)"), so
# `lhd lec` came back inconclusive (UNKNOWN) for both the ALU.prp-vs-ALU.sv and
# ALU.prp-vs-generated-ALU.v checks. Lowering each field to its own net breaks
# the false cycle: operation/inputx/inputy come straight from inputs; only
# result depends on them.
#
# This guards BOTH halves: (1) the generated Pyrope must carry per-field bundle
# nets (`io.operation`, not a flat `io:u197` bus), and (2) the struct design must
# cvc5-PROVE equivalent (strict + bmc) to the obvious struct-free twin — a
# regression to the flat bus would make the encoder fail and the strict LEC
# would not pass.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_lec_pstruct_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# Scalar packed struct whose '{...} write reads its own fields (the ALU `io`).
cat >"$W/pstruct.sv" <<'EOF'
module pstruct(
  input  [4:0]  a,
  input  [63:0] x,
  input  [63:0] y,
  output [63:0] r
);
  wire
    struct packed {logic [4:0] operation; logic [63:0] inputx; logic [63:0] inputy; logic [63:0] result; }
    io;
  wire        z = io.operation[4];
  wire [63:0] w = io.inputx + io.inputy;
  assign io = '{operation: a, inputx: x, inputy: y, result: w + z};
  assign r = io.result;
endmodule
EOF

# Struct-free ground truth (io.operation==a, io.inputx==x, io.inputy==y).
cat >"$W/pstruct_plain.sv" <<'EOF'
module pstruct(
  input  [4:0]  a,
  input  [63:0] x,
  input  [63:0] y,
  output [63:0] r
);
  wire        z = a[4];
  wire [63:0] w = x + y;
  assign r = w + z;
endmodule
EOF

# (1) Representation: the generated Pyrope must use per-field bundle nets, NOT a
#     flat packed-struct bus.
"$LHD" compile "$W/pstruct.sv" --top pstruct --emit-dir "pyrope:$W/prp" --workdir "$W/cw" -q >/dev/null 2>&1 \
  || fail "slang compile of the packed-struct design failed"
PRP="$W/prp/pstruct.prp"
[ -f "$PRP" ] || fail "no generated pyrope at $PRP"
grep -q 'io\.operation' "$PRP" || fail "expected per-field bundle net 'io.operation' in the generated pyrope; got:
$(cat "$PRP")"
grep -Eq 'wire +`?io`?:u1?[0-9][0-9]' "$PRP" \
  && fail "the struct was flattened to a single packed bus (e.g. 'wire io:u197') instead of a bundle:
$(cat "$PRP")"
echo "PASS: scalar packed struct lowers to per-field bundle nets"

# (2) Equivalence: the struct design must cvc5-PROVE equivalent (strict + bmc) to
#     the struct-free twin. A regression to the self-loop bus makes the encoder
#     fail and the strict LEC does NOT pass.
"$LHD" lec --set lec.strict=true --set lec.engine=bmc --top pstruct \
  --ref "$W/pstruct_plain.sv" --impl "$W/pstruct.sv" \
  --workdir "$W/lec" -q --result-json "$W/lec.json" \
  || fail "cvc5 LEC did not prove the packed-struct design equivalent (self-loop regression?): $(cat "$W/lec.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/lec.json" || fail "lec not pass: $(cat "$W/lec.json")"
echo "PASS: packed-struct design is cvc5-PROVEN equivalent to its struct-free twin"

# (3) SIGNED fields: a per-field leaf is declared at the field's width AND SIGN,
#     so a value written to a signed field must land in its signed range (fit via
#     truncate + sign-reinterpret, not an unsigned-pattern truncate that
#     overflows the declared signed range).
cat >"$W/psigned.sv" <<'EOF'
module psigned(
  input  signed [15:0] a,
  input  signed [15:0] b,
  output signed [31:0] r
);
  wire struct packed {logic signed [15:0] lo; logic signed [15:0] hi; logic signed [31:0] prod;} s;
  wire signed [31:0] p = s.lo * s.hi;
  assign s = '{lo: a, hi: b, prod: p + s.lo};
  assign r = s.prod;
endmodule
EOF
cat >"$W/psigned_plain.sv" <<'EOF'
module psigned(
  input  signed [15:0] a,
  input  signed [15:0] b,
  output signed [31:0] r
);
  wire signed [31:0] p = a * b;
  assign r = p + a;
endmodule
EOF
"$LHD" lec --set lec.strict=true --set lec.engine=bmc --top psigned \
  --ref "$W/psigned_plain.sv" --impl "$W/psigned.sv" \
  --workdir "$W/lecs" -q --result-json "$W/lecs.json" \
  || fail "cvc5 LEC did not prove the SIGNED packed-struct design equivalent (signed-field fit regression?): $(cat "$W/lecs.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/lecs.json" || fail "signed lec not pass: $(cat "$W/lecs.json")"
echo "PASS: signed packed-struct fields land in their declared signed range (cvc5-PROVEN)"

echo "PASS: scalar packed struct → bundle (no false self-loop)"
