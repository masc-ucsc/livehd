#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# pass.single_edge: a clocked MEMORY under the P=2 phase divider, validated
# against ICARUS VERILOG.
#
# Before this test the pass REFUSED every design that held a `Memory` cell at
# P > 1 ("would commit on every sub-step under a phase divider"), which blocked
# 17 of the 122 CORE-ET modules -- every register-file / array block -- from
# ever producing a certificate. The refusal was honest: a flop gets
# `enable &= (phase == slot)` and a memory did not, so an unslotted write port
# really would have committed on both sub-steps. The fix is the same gate on the
# memory's committing ports: every write port, and a sync-read port's read-data
# register; an async read is combinational and is deliberately NOT gated.
#
# The design is the minimal shape that exercises it:
#
#   posedge flop `st`      -> (clock, RISE)  slot 0
#   negedge flop `nf`      -> (clock, FALL)  slot 1   <- this is what forces P=2
#   memory `m`, written on the posedge from `st`, read asynchronously
#
# `nf = st` is the half-cycle transfer of flop_sim_posneg: at every period
# boundary qn == qp, which makes the SLOT ORDERING observable, not just P.
#
# TWO independent things are asserted.
#
# (1) TRACE-LEVEL VALIDATION against iverilog, at PERIOD BOUNDARIES. Edge
#     normalization is not cycle-preserving -- one source cycle becomes P
#     sub-steps -- so no cycle-accurate equivalence checker (lhd lec, lgcheck)
#     can validate it. What can: run the SOURCE Verilog (real `negedge`, memory
#     on `posedge`) and the NORMALIZED Verilog (posedge flops + a phase divider)
#     side by side, the normalized side clocked P times per source period, and
#     compare at period boundaries. This is the task's binding cross-model
#     rule: "PROVEN before AND after" cannot see a transformation that is wrong
#     but applied identically to both sides.
#
#     The memory read `rd` is the observation that matters. If the write were
#     gated to the WRONG slot (1 instead of 0) it would commit at the fall, after
#     `st` has already advanced, and write the new value instead of the old --
#     visible at the next boundary. If it were not gated at all it would write on
#     both sub-steps; that is what the P=1 negative control below exposes.
#
# (2) The pass FIRES and reports the memory: `P=2 slots ... 1 memory(ies)
#     slotted`, and the normalized emission holds no `negedge`.
#
# Written in PYROPE on the module's implicit clock, exactly the shape of
# inou/prp/tests/sim/mem_sim_negedge_write.prp (a live sim regression), so the
# fixture is known to compile and the memory is known to import as a clocked
# `Memory` cell rather than a combinational array.

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

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() {
  echo "FAIL: $*"
  exit 1
}

# ---- the design --------------------------------------------------------------
cat > "$W/memslot.prp" <<'EOF'
pub mod memslot(d:u8, we:bool, wa:u2, ra:u2) -> (rd:u8@[1], qp:u8@[1], qn:u8@[2]) {
  reg st:u8 = 0                     // posedge flop -> slot 0
  reg nf:u8:[posclk=false] = 0      // negedge flop -> slot 1: forces P=2
  reg m:[4]u8 = nil                 // clocked memory; the write commits on the posedge
  st = d
  nf = st                           // half-cycle transfer: qn == qp at every boundary
  qp = st
  qn = nf
  rd = m[ra]                        // asynchronous read: combinational, must NOT be gated
  if we {
    m[wa] = st                      // the write port: must commit in slot 0 only
  }
}
EOF

rm -rf "$W/lg_src"
"$LHD" compile "$W/memslot.prp" --top memslot --emit-dir "lg:$W/lg_src" --workdir "$W/cw" \
  >"$W/compile.log" 2>&1 || { tail -8 "$W/compile.log"; fail "compile of memslot.prp failed"; }

# ---- (2): the pass fires, slots the memory, and lowers to P=2 -----------------
rm -rf "$W/lg_norm"
"$LHD" pass single_edge --top memslot "lg:$W/lg_src" --emit-dir "lg:$W/lg_norm" \
  --workdir "$W/pw" >"$W/pass.log" 2>&1 || { tail -8 "$W/pass.log"; fail "pass single_edge refused or failed on a memory design (the pre-fix behaviour)"; }
grep -q "single-edge-applied" "$W/pass.log" || { tail -6 "$W/pass.log"; fail "the pass did not fire (a negedge flop should force P=2)"; }
grep -q "P=2 slots" "$W/pass.log" || { grep -o "P=[0-9]* slots[^\"]*" "$W/pass.log" | head -1; fail "the design did not lower into a 2-slot time base"; }
grep -q "1 memory(ies) slotted" "$W/pass.log" \
  || { grep -o "P=[0-9]* slots[^\"]*" "$W/pass.log" | head -1; fail "the memory was not slot-gated (the pass did not count it)"; }
echo "ok: pass.single_edge lowered a clocked memory under P=2 (1 memory slotted)"

emit() { # <lgdir> <outdir>
  rm -rf "$2"
  "$LHD" compile "lg:$1" --top memslot --emit-dir "verilog:$2" --workdir "$W/vw_$(basename "$2")" \
    >"$W/e_$(basename "$2").log" 2>&1 \
    || { tail -6 "$W/e_$(basename "$2").log"; fail "verilog emission from $1 failed"; }
}
emit "$W/lg_src"  "$W/v_src"
emit "$W/lg_norm" "$W/v_norm"

cat "$W"/v_src/*.v > "$W/src.v"
# Rename the normalized copy so both instantiate in one testbench, and drop its
# copy of the `include for the cgen memory primitive -- src.v already pulls it
# in, and iverilog has no include guard for it.
sed -e 's/^module memslot(/module memslot_n(/' -e '/^`include "cgen_memory_/d' "$W"/v_norm/*.v > "$W/norm.v"

grep -q "negedge" "$W/src.v"  || fail "the SOURCE emission lost its negedge -- nothing independent is being compared"
grep -q "negedge" "$W/norm.v" && fail "the NORMALIZED emission still holds a negedge flop (a partial lowering)"
echo "ok: source keeps its negedge, normalized has none"

# The emitted module's clock/reset port names (implicit-clock Pyrope module).
CLK=$(grep -oE '\b(clock|clk)\b' "$W/src.v" | head -1)
[ -n "$CLK" ] || fail "could not find the clock port in the emitted source Verilog"
grep -q '\breset\b' "$W/src.v" || fail "could not find the reset port in the emitted source Verilog"

# ---- (1): trace-level validation against iverilog -----------------------------
if ! command -v iverilog >/dev/null 2>&1 || ! command -v vvp >/dev/null 2>&1; then
  echo "note: iverilog/vvp not found -- the INDEPENDENT trace validation was SKIPPED."
  echo "PASS: single_edge_memory_slot_test (structural legs only)"
  exit 0
fi

cat > "$W/tb.v" <<EOF
\`timescale 1ns/1ps
// HALF = the normalized clock's half-period. 5 gives it TWO posedges per source
// period (P=2, correct); 10 gives it one, the P=1 time base -- the negative
// control below, which must FAIL.
\`ifndef HALF
\`define HALF 5
\`endif
module tb;
  reg clk_s = 0, clk_n = 0, reset = 1;
  reg [7:0] d = 0;
  reg       we = 0;
  reg [1:0] wa = 0, ra = 0;
  wire [7:0] s_rd, s_qp, s_qn, n_rd, n_qp, n_qn;
  integer errs = 0, p;

  always #10      clk_s = ~clk_s;   // source:     rise 10+20p, fall 20+20p
  always #(\`HALF) clk_n = ~clk_n;   // normalized: slot0 15+20p, slot1 25+20p

  memslot   src(.${CLK}(clk_s), .reset(reset), .d(d), .we(we), .wa(wa), .ra(ra), .rd(s_rd), .qp(s_qp), .qn(s_qn));
  memslot_n nrm(.${CLK}(clk_n), .reset(reset), .d(d), .we(we), .wa(wa), .ra(ra), .rd(n_rd), .qp(n_qp), .qn(n_qn));

  task chk(input [63:0] nm, input [7:0] a, input [7:0] b);
    if (a !== b) begin
      errs = errs + 1;
      if (errs < 6) \$display("MISMATCH %0s @%0t source=%0d normalized=%0d", nm, \$time, a, b);
    end
  endtask

  initial begin
    // Stimulus changes at 6+20p, STABLE across every sampling edge of the period
    // on both sides (source rise 10+20p / fall 20+20p; normalized slot0 15+20p /
    // slot1 25+20p).
    reset = 1; d = 0; we = 0; wa = 0; ra = 0;
    #26 d = 8'd37; we = 1; wa = 2'd1; ra = 2'd0;
    #1  reset = 0;   // t=27: after every period-0 edge, so the divider starts period 1 at slot 0
    #11;             // t=38 = 18+20*1
    for (p = 1; p < 40; p = p + 1) begin
      // Posedge-committed state, settled after both sides' slot-0 commit.
      chk("qp", s_qp, n_qp);
      chk("rd", s_rd, n_rd);   // the memory: async read of what the write port committed
      #8  begin                // t=26+20p
        d  = (p * 8'd37 + 8'd11) & 8'hff;
        we = (p % 3) != 2;     // skip a write every third period
        wa = p[1:0];
        ra = (p + 3);          // read the address written LAST period (mod 4)
      end
      #2  chk("qn", s_qn, n_qn);   // t=28+20p: negedge state, settled after both falls
      #10;                         // t=38+20p
    end
    if (errs == 0) \$display("SINGLE_EDGE_MEM_DIFF_OK");
    else           \$display("SINGLE_EDGE_MEM_DIFF_FAIL errs=%0d", errs);
    \$finish;
  end
endmodule
EOF

# The emitted memory instantiates `cgen_memory_*.v` from ware/rtl (a bazel data
# dep of this test, so it is present under runfiles as well as in a checkout).
RTL_INC="ware/rtl"
[ -d "$RTL_INC" ] || RTL_INC="$(cd "$(dirname "$0")/../.." && pwd)/ware/rtl"
[ -d "$RTL_INC" ] || fail "could not locate ware/rtl for the cgen memory primitives"

run_iv() { # <label> <extra iverilog args...>
  local label=$1
  shift
  iverilog -g2012 -I "$RTL_INC" "$@" -o "$W/$label.vvp" "$W/src.v" "$W/norm.v" "$W/tb.v" >"$W/iv_$label.log" 2>&1 \
    || { cat "$W/iv_$label.log"; fail "iverilog failed to elaborate ($label)"; }
  vvp "$W/$label.vvp" >"$W/vvp_$label.log" 2>&1
  cat "$W/vvp_$label.log"
}

out="$(run_iv p2)"
grep -q "SINGLE_EDGE_MEM_DIFF_OK" <<<"$out" \
  || { echo "$out" | head -8; fail "the NORMALIZED design does not match the real negedge/memory source under iverilog -- the memory slot gate is wrong"; }
echo "ok: source vs normalized agree over 39 periods under iverilog, memory read included (independent oracle)"

# Negative control: the normalized side in the P=1 time base. Its memory gate
# `enable & (phase == 0)` now fires every OTHER period and its negedge flop lags,
# so this must FAIL -- or the comparison above proves nothing about the divider.
out="$(run_iv p1 -DHALF=10)"
grep -q "SINGLE_EDGE_MEM_DIFF_FAIL" <<<"$out" \
  || fail "the differential harness still passed with the normalized side clocked at 1x -- it cannot detect a wrong time base"
echo "ok: forcing the normalized side to a P=1 time base FAILS (the harness discriminates)"

echo "PASS: single_edge_memory_slot_test"
