#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.single_edge: a memory whose WRITE PORT COMMITS ON THE FALLING EDGE is
# lowered the way a negedge flop is, and validated against ICARUS VERILOG.
#
# A negedge FLOP is lowered by RETYPING it to posedge AND gating it to the fall
# slot; the two together make the result single-edge. A falling memory now gets
# the same three moves: its `posclk` becomes 1, every write port's clock_pin is
# rebound to the reference clock root (removing any inversion in the cone), and
# each write port's enable is gated to the FALL slot (slot 1). Any one of them
# alone is a half-lowering: gating alone leaves `.clk(~(clock))` on the memory
# instance, and a retype alone moves the write half a cycle early.
#
# History. This file used to assert a REFUSAL ("until the rebinding is
# implemented"). Before that, `lc::commit_class_of(n, &clocks)` returned nullopt
# for every Memory, the class was empty, and a falling memory was silently gated
# to SLOT 0. The per-port classification fixed the slot; the retype and rebind
# are what let the pass accept it.
#
# The design:
#
#   posedge flop `st`      -> (clock, RISE)  slot 0
#   negedge flop `nf`      -> (clock, FALL)  slot 1
#   memory `m`, written on the NEGEDGE from `st`, read asynchronously
#
# The memory read `rd` is the observation that matters. The write commits at the
# fall, AFTER `st` advanced at the rise, so it writes the NEW `st`; gated to the
# wrong slot (0) it would write the old one, which the next boundary shows.
#
# Three things are asserted:
#   (1) the pass FIRES and reports the memory moved to the fall slot, and the
#       normalized emission holds no negedge and no inverted memory clock;
#   (2) source vs normalized agree at every period boundary under iverilog;
#   (3) two controls FAIL: the normalized side in a P=1 time base, and the same
#       design with a RISING memory against this normalized one (so the harness
#       really sees which edge the memory is written on).
#
# The memory-only case (no other negedge state) is covered too: a negedge
# memory alone must still make P = 2 and get a reference clock.
#
# The external-simulator legs, (2) and (3), run only with LHD_EXTERNAL_SIM=1.

set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then LHD=./bazel-bin/lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

W="${TEST_TMPDIR:-}"
if [ -z "$W" ]; then
  W="$(cd "$(dirname "$0")/../.." && pwd)/generated/single_edge_negedge_mem.$$"
fi
mkdir -p "$W"
trap 'rm -rf "$W"' EXIT

fail() { echo "FAIL: $*"; exit 1; }

cat > "$W/memneg.prp" <<'EOF'
pub mod memneg(d:U8, we:Bool, wa:U2, ra:U2) -> (rd:U8@[1], qp:U8@[1], qn:U8@[2]) {
  reg st:U8 = 0                          // posedge flop -> slot 0
  reg nf:U8:[posclk=false] = 0           // negedge flop -> slot 1
  reg m:[4]U8:[posclk=false] = nil       // the memory COMMITS ON THE FALL
  st = d
  nf = st
  qp = st
  qn = nf
  rd = m[ra]                             // asynchronous read: not gated
  if we {
    m[wa] = st                           // write port: must land in slot 1
  }
}
EOF
# The control: the identical design with a RISING memory.
sed -e 's/reg m:\[4\]U8:\[posclk=false\] = nil/reg m:[4]U8 = nil/' -e 's/pub mod memneg(/pub mod mempos(/' \
  "$W/memneg.prp" > "$W/mempos.prp"
grep -q 'reg m:\[4\]U8 = nil' "$W/mempos.prp" || fail "the rising control was not derived"

compile_lg() { # <top>
  rm -rf "$W/lg_$1"
  "$LHD" compile "$W/$1.prp" --top "$1" --emit-dir "lg:$W/lg_$1" --workdir "$W/cw_$1" \
    >"$W/compile_$1.log" 2>&1 || { tail -8 "$W/compile_$1.log"; fail "compile of $1.prp failed"; }
}
compile_lg memneg
compile_lg mempos

# ---- (1): the pass fires and moves the memory to the fall slot ---------------
rm -rf "$W/lg_norm"
"$LHD" pass single_edge --top memneg "lg:$W/lg_memneg" --emit-dir "lg:$W/lg_norm" \
  --workdir "$W/pw" >"$W/pass.log" 2>&1 \
  || { tail -4 "$W/pass.log"; fail "pass single_edge refused a falling-edge memory"; }
grep -q "P=2 slots" "$W/pass.log" || { grep -o "P=[0-9]* slots[^\"]*" "$W/pass.log" | head -1; fail "not a 2-slot time base"; }
grep -q "1 memory(ies) slotted" "$W/pass.log" \
  || { grep -o "P=[0-9]* slots[^\"]*" "$W/pass.log" | head -1; fail "the falling memory was not slot-gated"; }
grep -q "1 falling-edge memory(ies) moved to the fall slot" "$W/pass.log" \
  || { grep -o "P=[0-9]* slots[^\"]*" "$W/pass.log" | head -1; fail "the falling memory was not retyped"; }
echo "ok: pass.single_edge lowered the falling memory (retyped, rebound, gated to the fall slot)"

emit() { # <lgdir> <top> <outdir>
  rm -rf "$3"
  "$LHD" compile "lg:$1" --top "$2" --emit-dir "verilog:$3" --workdir "$W/vw_$(basename "$3")" \
    >"$W/e_$(basename "$3").log" 2>&1 \
    || { tail -6 "$W/e_$(basename "$3").log"; fail "verilog emission from $1 failed"; }
}
emit "$W/lg_memneg" memneg "$W/v_src"
emit "$W/lg_norm" memneg "$W/v_norm"
emit "$W/lg_mempos" mempos "$W/v_pos"

cat "$W"/v_src/*.v > "$W/src.v"
# Rename the normalized copy so both instantiate in one testbench, and drop its
# copy of the `include for the cgen memory primitive -- src.v already pulls it in.
sed -e 's/^module memneg(/module memneg_n(/' -e '/^`include "cgen_memory_/d' "$W"/v_norm/*.v > "$W/norm.v"
sed -e '/^`include "cgen_memory_/d' "$W"/v_pos/*.v > "$W/pos.v"

grep -q "negedge" "$W/src.v" || fail "the SOURCE emission lost its negedge -- nothing independent is being compared"
grep -q "negedge" "$W/norm.v" && fail "the NORMALIZED emission still holds a negedge (a partial lowering)"
grep -qE '~ *\( *(clock|clk) *\)' "$W/norm.v" && fail "the NORMALIZED memory still has an inverted clock (gating alone)"
echo "ok: source keeps its negedge, normalized has none and no inverted memory clock"

# ---- the memory-only case: a negedge memory alone still makes P = 2 ----------
cat > "$W/memonly.prp" <<'EOF'
pub mod memonly(d:U8, we:Bool, wa:U2, ra:U2) -> (rd:U8@[0]) {
  reg m:[4]U8:[posclk=false] = nil
  rd = m[ra]
  if we {
    m[wa] = d
  }
}
EOF
compile_lg memonly
"$LHD" pass single_edge --top memonly "lg:$W/lg_memonly" --emit-dir "lg:$W/lg_memonly_norm" \
  --workdir "$W/pwo" >"$W/pass_only.log" 2>&1 \
  || { tail -4 "$W/pass_only.log"; fail "a design whose only state is a negedge memory was refused"; }
grep -q "P=2 slots" "$W/pass_only.log" \
  || { grep -o "single-edge-[a-z]*[^\"]*\"[^\"]*\"[^\"]*" "$W/pass_only.log" | head -2; fail "a negedge memory alone did not make P = 2 (the pass skipped it)"; }
grep -q "1 falling-edge memory(ies) moved to the fall slot" "$W/pass_only.log" || fail "the lone negedge memory was not retyped"
echo "ok: a negedge memory alone triggers the pass and gets the fall slot"

# The emitted module's clock port name (implicit-clock Pyrope module).
CLK=$(grep -oE '\b(clock|clk)\b' "$W/src.v" | head -1)
[ -n "$CLK" ] || fail "could not find the clock port in the emitted source Verilog"
grep -q '\breset\b' "$W/src.v" || fail "could not find the reset port in the emitted source Verilog"

# ---- (2) and (3): trace-level validation against iverilog ---------------------
# The external-simulator leg runs only with LHD_EXTERNAL_SIM=1 (iverilog/vvp).
if [ -z "${LHD_EXTERNAL_SIM:-}" ]; then
  echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
  echo "PASS: single_edge_memory_negedge_slot_test (structural legs only)"
  exit 0
fi
command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1 \
  || fail "LHD_EXTERNAL_SIM is set but iverilog/vvp are not on PATH"

cat > "$W/tb.v" <<EOF
\`timescale 1ns/1ps
// HALF = the normalized clock's half-period: 5 gives two posedges per source
// period (P=2); 10 gives one (the P=1 control). REF picks the reference model:
// memneg (the real negedge source) or mempos (the rising control).
\`ifndef HALF
\`define HALF 5
\`endif
\`ifndef REF
\`define REF memneg
\`endif
module tb;
  reg clk_s = 0, clk_n = 0, reset = 1;
  reg [7:0] d = 0;
  reg       we = 0;
  reg [1:0] wa = 0, ra = 0;
  wire [7:0] s_rd, s_qp, s_qn, n_rd, n_qp, n_qn;
  integer errs = 0, p;

  always #10 clk_s = ~clk_s;   // source:     rise 10+20p, fall 20+20p
  // normalized: slot0 15+20p, slot1 25+20p (the divider starts at slot 0).
  initial begin #10; forever #(\`HALF) clk_n = ~clk_n; end

  \`REF    src(.${CLK}(clk_s), .reset(reset), .d(d), .we(we), .wa(wa), .ra(ra), .rd(s_rd), .qp(s_qp), .qn(s_qn));
  memneg_n nrm(.${CLK}(clk_n), .reset(reset), .d(d), .we(we), .wa(wa), .ra(ra), .rd(n_rd), .qp(n_qp), .qn(n_qn));

  task chk(input [63:0] nm, input [7:0] a, input [7:0] b);
    if (a !== b) begin
      errs = errs + 1;
      if (errs < 6) \$display("MISMATCH %0s @%0t source=%0d normalized=%0d", nm, \$time, a, b);
    end
  endtask

  initial begin
    // Stimulus changes at 6+20p, stable across every sampling edge of the period.
    reset = 1; d = 0; we = 0; wa = 0; ra = 0;
    #26 d = 8'd37; we = 1; wa = 2'd1; ra = 2'd0;
    #1  reset = 0;
    #11;             // t=38 = 18+20*1
    for (p = 1; p < 40; p = p + 1) begin
      chk("qp", s_qp, n_qp);
      chk("rd", s_rd, n_rd);   // the memory: what the FALL-slot write committed
      #8  begin                // t=26+20p
        d  = (p * 8'd37 + 8'd11) & 8'hff;
        we = (p % 3) != 2;     // skip a write every third period
        wa = p[1:0];
        ra = (p + 3);          // read the address written LAST period (mod 4)
      end
      #2  chk("qn", s_qn, n_qn);
      #10;
    end
    if (errs == 0) \$display("SINGLE_EDGE_NEGMEM_DIFF_OK");
    else           \$display("SINGLE_EDGE_NEGMEM_DIFF_FAIL errs=%0d", errs);
    \$finish;
  end
endmodule
EOF

RTL_INC="ware/rtl"
[ -d "$RTL_INC" ] || RTL_INC="$(cd "$(dirname "$0")/../.." && pwd)/ware/rtl"
[ -d "$RTL_INC" ] || fail "could not locate ware/rtl for the cgen memory primitives"

run_iv() { # <label> <extra iverilog args...>
  local label=$1
  shift
  iverilog -g2012 -I "$RTL_INC" "$@" -o "$W/$label.vvp" "$W/src.v" "$W/pos.v" "$W/norm.v" "$W/tb.v" >"$W/iv_$label.log" 2>&1 \
    || { cat "$W/iv_$label.log"; fail "iverilog failed to elaborate ($label)"; }
  vvp "$W/$label.vvp" >"$W/vvp_$label.log" 2>&1
  cat "$W/vvp_$label.log"
}

out="$(run_iv p2)"
grep -q "SINGLE_EDGE_NEGMEM_DIFF_OK" <<<"$out" \
  || { echo "$out" | head -8; fail "the NORMALIZED design does not match the real negedge-memory source under iverilog"; }
echo "ok: source vs normalized agree over 39 periods under iverilog, memory read included"

out="$(run_iv p1 -DHALF=10)"
grep -q "SINGLE_EDGE_NEGMEM_DIFF_FAIL" <<<"$out" \
  || fail "the harness still passed with the normalized side clocked at 1x -- it cannot detect a wrong time base"
echo "ok: control 1 -- the normalized side in a P=1 time base FAILS"

out="$(run_iv rise -DREF=mempos)"
grep -q "SINGLE_EDGE_NEGMEM_DIFF_FAIL" <<<"$out" \
  || fail "a RISING-memory source matched the normalized falling memory -- the harness cannot see the memory's edge"
grep -q "MISMATCH rd" <<<"$out" || fail "the rising control differed, but not on the memory read"
echo "ok: control 2 -- the same design with a RISING memory FAILS, on the memory read"

echo "PASS: single_edge_memory_negedge_slot_test"
