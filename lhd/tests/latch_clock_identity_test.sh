#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# todo/livehd/2f-latch M2 — LIVE test for the latch's cell identity and the
# consumers that had to learn about it.
#
# THE HEADLINE IS THE NEGATIVE CASE. M2 set out to let Pyrope author an
# active-low latch enable via an attribute, and implementing it as a bare
# polarity-pin flip produced a SILENT MISCOMPILE — the exact "posclk double
# negation" the task page predicted, and one a symmetric before/after gate is
# structurally incapable of seeing (both sides get the same wrong flip and
# happily prove equal). It was caught only by LEC-ing against an INDEPENDENT
# golden. The mechanism, worth stating once because it is not obvious:
#
#   tolg lowers `if cond { l = d }` to BOTH
#       din = cond ? d : q      and      enable = cond
#   so the enable is active-HIGH *by construction*. Flipping only the polarity
#   pin yields `if (!cond) q <= (cond ? d : q)`: while the latch is transparent
#   (cond==0) din resolves to q, so it writes ITSELF forever and never captures
#   d. The emitted Verilog looks entirely reasonable.
#
# So the attribute is REFUSED from Pyrope rather than half-honored, and this
# test pins BOTH sides of that: the refusal, and the fact that the spelling it
# points you to (`if !g`) really is correct against an independent golden. The
# polarity still lives on the CELL (pid 6) for the yosys importer, whose
# raw-D + EN shape has no hold mux and for which the flip IS sound.
#
# Also covered: pass/abc must treat a Latch as a boundary (it used to reject the
# WHOLE region with "cell 'latch' has no combinational bit-blast yet"), and a
# color+partition+compile flow must emit Verilog that elaborates.

set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then
    LHD=./bazel-bin/lhd/lhd
  else
    echo "FAIL: could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi
LIB=inou/prp/tests/abc/test.lib
[ -f "$LIB" ] || { echo "FAIL: missing liberty $LIB"; exit 1; }

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() { echo "FAIL: $*"; exit 1; }

HAVE_IVERILOG=0
command -v iverilog >/dev/null 2>&1 && HAVE_IVERILOG=1

# ---- 1: active-low-enable ATTRIBUTE is refused, not silently miscompiled -----
cat > "$W/attr.prp" <<'EOF'
pub mod enlow(g:bool, d:u8) -> (q:u8@[0]) {
  reg l:u8:[latch=true, enable_high=false]
  if g {
    l = d
  }
  q = l
}
EOF

out="$("$LHD" compile "$W/attr.prp" --emit verilog:"$W/attr.v" --workdir "$W/w_attr" -q 2>&1)"
[ $? -ne 0 ] || { echo "$out" | tail -3; fail "enable_high=false compiled — it would emit a latch that writes itself and never captures din"; }
grep -q "SILENTLY DROPPED" <<<"$out" || { echo "$out" | tail -3; fail "enable_high=false failed for the wrong reason (expected the directed refusal)"; }
grep -q "if !g" <<<"$out" || fail "the refusal must point at the spelling that works (\`if !g { ... }\`)"
echo "ok: enable_high=false is REFUSED with a directed diagnostic"

# ---- 2: and the spelling it recommends is genuinely correct ------------------
# Independent oracle, NOT our own encoder on both sides: lgcheck's bounded miter
# is polarity-DISCRIMINATING for latches (see lec_latch_polarity_test.sh), so a
# PROVEN here is real and a REFUTED against the flipped golden proves the check
# is not vacuous.
cat > "$W/ok.prp" <<'EOF'
pub mod enlow(g:bool, d:u8) -> (q:u8@[0]) {
  reg l:u8:[latch=true]
  if !g {
    l = d
  }
  q = l
}
EOF
cat > "$W/gold_low.v" <<'EOF'
module enlow(input g, input [7:0] d, output reg [7:0] q);
  always_latch begin
    if (!g)
      q <= d;
  end
endmodule
EOF
cat > "$W/gold_high.v" <<'EOF'
module enlow(input g, input [7:0] d, output reg [7:0] q);
  always_latch begin
    if (g)
      q <= d;
  end
endmodule
EOF

"$LHD" compile "$W/ok.prp" --emit verilog:"$W/ok.v" --workdir "$W/w_ok" -q >"$W/ok.log" 2>&1 \
  || { tail -3 "$W/ok.log"; fail "the \`if !g\` active-low spelling does not compile"; }

"$LHD" lec --set formal.solver=lgyosys --impl verilog:"$W/ok.v" --ref verilog:"$W/gold_low.v" \
  --top enlow --workdir "$W/w_lo" -q >"$W/lo.log" 2>&1 \
  || { tail -3 "$W/lo.log"; fail "\`if !g\` is NOT equivalent to its active-low golden"; }
echo "ok: the \`if !g\` spelling PROVES against an active-low golden"

# Vacuity guard: the same oracle must REFUTE the opposite polarity, or the
# PROVEN above says nothing.
if "$LHD" lec --set formal.solver=lgyosys --impl verilog:"$W/ok.v" --ref verilog:"$W/gold_high.v" \
     --top enlow --workdir "$W/w_hi" -q >"$W/hi.log" 2>&1; then
  fail "the oracle PROVED active-low == active-high — it is blind here, so check 2 is vacuous"
fi
echo "ok: the same oracle REFUTES the flipped polarity (check 2 is not vacuous)"

# ---- 3: pass/abc treats a Latch as a boundary --------------------------------
# Pre-M2 this errored `unsupported-cell: cell 'latch' ... has no combinational
# bit-blast yet` and took the WHOLE region down with it. A Latch must be a
# boundary in BOTH map modes: an ABC/AIGER "latch" is an edge-triggered
# unit-delay register and ABC's BLIF reader silently discards the .latch control
# tokens, so letting a real latch cross into ABC would be a silent mismodel.
cat > "$W/abc.prp" <<'EOF'
pub mod abclatch(en:bool, a:u8, b:u8) -> (q:u8@[0]) {
  reg l:u8:[latch=true]
  if en {
    l = a & b
  }
  q = l | a
}
EOF

"$LHD" compile "$W/abc.prp" --top abclatch --recipe O1 --emit-dir lg:"$W/abc_lg" --workdir "$W/w_ac1" -q \
  >"$W/ac1.log" 2>&1 || { tail -3 "$W/ac1.log"; fail "abc fixture does not compile"; }
"$LHD" pass abc --top abclatch lg:"$W/abc_lg" --emit-dir lg:"$W/abc_net" --set pass.abc.library="$LIB" \
  --workdir "$W/w_ac2" -q >"$W/ac2.log" 2>&1 \
  || { tail -3 "$W/ac2.log"; fail "pass abc REJECTS a region containing a latch (M2 boundary regression)"; }
ls "$W/abc_net"/graph_* >/dev/null 2>&1 || fail "pass abc emitted no mapped netlist for the latch region"
echo "ok: pass abc maps a region containing a latch"

# The latch must survive NATIVE in the netlist — mapped-away or bit-blasted
# would be the silent mismodel described above.
"$LHD" compile lg:"$W/abc_net" --top abclatch --emit verilog:"$W/abc_net.v" --workdir "$W/w_ac3" -q \
  >"$W/ac3.log" 2>&1 || { tail -3 "$W/ac3.log"; fail "cannot emit the mapped netlist"; }
grep -q "always_latch" "$W/abc_net.v" \
  || fail "the latch did NOT survive abc as a native cell (it must be boxed, never bit-blasted)"
echo "ok: the latch survives abc as a native boxed cell"

# NOTE: the post-abc netlist cannot yet be LEC-PROVEN against its pre-abc twin —
# the native cvc5 encoder still refuses the Latch cell (that is M4's job, and it
# correctly exits nonzero rather than pretending). The flop-shaped twin of this
# exact design DOES prove through the same flow, which is what tells us the
# methodology (not the latch handling) is sound.

# ---- 4: color + partition + compile emits elaborable Verilog -----------------
"$LHD" compile "$W/ok.prp" --top enlow --recipe O1 --emit-dir lg:"$W/p_lg" --workdir "$W/w_p1" -q \
  >"$W/p1.log" 2>&1 || { tail -3 "$W/p1.log"; fail "partition fixture does not compile"; }
"$LHD" pass color synth --top enlow lg:"$W/p_lg" --workdir "$W/w_p2" -q >"$W/p2.log" 2>&1 \
  || { tail -3 "$W/p2.log"; fail "pass color synth failed on a latch design"; }
"$LHD" pass partition --top enlow lg:"$W/p_lg" --emit-dir lg:"$W/p_re" --workdir "$W/w_p3" -q >"$W/p3.log" 2>&1 \
  || { tail -3 "$W/p3.log"; fail "pass partition failed on a latch design"; }
"$LHD" compile lg:"$W/p_re" --top enlow --emit verilog:"$W/p.v" --workdir "$W/w_p4" -q >"$W/p4.log" 2>&1 \
  || { tail -3 "$W/p4.log"; fail "cannot emit verilog from the partitioned latch design"; }
if [ $HAVE_IVERILOG -eq 1 ]; then
  iverilog -g2012 -o /dev/null "$W/p.v" 2>"$W/p.iv" \
    || { cat "$W/p.iv"; fail "partitioned latch design emits verilog iverilog rejects"; }
fi
echo "ok: color + partition + compile emits elaborable Verilog for a latch design"

echo "PASS: latch_clock_identity_test"
