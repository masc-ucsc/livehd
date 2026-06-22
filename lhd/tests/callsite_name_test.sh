#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Call-site `name=` attribute (`f::[name=inst](args)`) + reg local-name override
# (`reg x:[name="reg_x"]`). Both exist so a Pyrope design can reproduce the
# Verilog/firtool hierarchical flop names that LEC uses to put corresponding
# state in 1:1 correspondence (a `mod` instantiated twice as pipeA/pipeB must
# yield distinct, hierarchy-prefixed flop names — not one shared local name).
#
# Asserts:
#   (1) a call-site `name=` overrides the dst-derived Sub instance name, so a
#       `mod` instantiated twice gets the two requested instance names;
#   (2) the flops inside read with the hierarchical name `<inst>.<reg>`, and the
#       reg local override renames the leaf (`reg_mem_writedata`), so the full
#       name matches dino_v1's `pipeB_ex_mem.reg_mem_writedata`;
#   (3) the design is PROVEN equivalent (cvc5) to a hierarchical Verilog
#       reference with the same two instances.

set -u
LHD=lhd/lhd
W="${TEST_TMPDIR:-/tmp/lhd_callsite_name_$$}"
mkdir -p "$W"
fail() { echo "FAIL: $*" >&2; exit 1; }

# ── The design under test: a reusable stage register `mod`, instantiated twice
#    with explicit call-site instance names; the flop carries a local override.
cat >"$W/csdut.prp" <<'EOF'
mod stage(d:u8) -> (q:u8@[1]) {
  reg m:u8:[name="reg_mem_writedata"] = 0
  q = m
  m = d
}
mod csdut(da:u8, db:u8) -> (qa:u8@[], qb:u8@[]) {
  qa = stage::[name=pipeA_ex_mem](da)
  qb = stage::[name=pipeB_ex_mem](db)
}
EOF

# Hierarchical Verilog reference with the same two instance names.
cat >"$W/csdut_ref.v" <<'EOF'
module stage(input clock, input reset, input [7:0] d, output [7:0] q);
  reg [7:0] reg_mem_writedata;
  always @(posedge clock) if (reset) reg_mem_writedata <= 8'd0;
                          else        reg_mem_writedata <= d;
  assign q = reg_mem_writedata;
endmodule
module csdut(input clock, input reset, input [7:0] da, input [7:0] db,
             output [7:0] qa, output [7:0] qb);
  stage pipeA_ex_mem(.clock(clock), .reset(reset), .d(da), .q(qa));
  stage pipeB_ex_mem(.clock(clock), .reset(reset), .d(db), .q(qb));
endmodule
EOF

# ── (1)+(2) compile to an LGraph and inspect the register hierarchy ───────────
"$LHD" compile "$W/csdut.prp" --top csdut --emit-dir "lg:$W/impl/" \
  --set compile.formal.mode=none --workdir "$W/implw" -q >/dev/null 2>&1 \
  || fail "impl (.prp) compile failed"

TREE=$("$LHD" tool tree --top csdut.csdut "lg:$W/impl/" --target kind:register 2>&1)
echo "$TREE"

# (1) the requested instance names appear (NOT the default dst-derived qa/qb).
echo "$TREE" | grep -q 'pipeA_ex_mem' || fail "instance pipeA_ex_mem missing — call-site name= ignored"
echo "$TREE" | grep -q 'pipeB_ex_mem' || fail "instance pipeB_ex_mem missing — call-site name= ignored"
if echo "$TREE" | grep -Eq '^\s+(qa|qb)\b.*stage'; then
  fail "Sub kept the dst-derived name (qa/qb) — call-site name= did not override"
fi

# (2) the reg leaf is the override name, two flops (one per instance).
nflop=$(echo "$TREE" | grep -c 'reg_mem_writedata.*flop')
[ "$nflop" -eq 2 ] || fail "expected 2 'reg_mem_writedata' flops (one per instance), found $nflop"

echo "PASS(names): pipeA_ex_mem / pipeB_ex_mem instances, reg_mem_writedata flops"

# ── (3) prove equivalent to the hierarchical Verilog reference (cvc5) ──────────
"$LHD" compile --reader slang "$W/csdut_ref.v" --top csdut \
  --emit-dir "lg:$W/ref/" --workdir "$W/refw" -q >/dev/null 2>&1 \
  || fail "reference (.v) compile failed"

"$LHD" lec --ref "lg:$W/ref/" --impl "lg:$W/impl/" \
  --ref-top csdut --impl-top csdut.csdut \
  --workdir "$W/lec" -q --result-json "$W/r.json" \
  || fail "cvc5 lec did NOT prove equivalent: $(cat "$W/r.json" 2>/dev/null)"
grep -q '"status":"pass"' "$W/r.json" || fail "lec not pass: $(cat "$W/r.json")"
echo "PASS(lec): call-site-named hierarchy == Verilog reference (cvc5)"

echo "ALL PASS: call-site name= + reg name override"
