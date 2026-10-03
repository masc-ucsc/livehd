#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The SHIPPED multi-read model, driven DIRECTLY.
#
# mem_sync_read_hold_test covers one read port and two independent single-read
# INSTANCES. Neither exercises rd_enable_1 against rd_enable_0 INSIDE one
# model, which is where gating port 1 with port 0's enable would hide -- and
# getting a single array with two sync read ports through the RTL frontend
# does not work (yosys replaces a small array with registers, and a 256-entry
# version emits no cgen_memory instance). So this drives the wrapper itself
# with a Verilog testbench and asserts EXACT held/read values per port.
#
# Contract: on a sync read port (LATENCY_0==1) RD_EN is a clock enable, so a
# disabled cycle HOLDS the output register.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." 2>/dev/null && pwd)"
MODEL=""
for c in "${TEST_SRCDIR:-}/_main/ware/rtl/cgen_memory_2rd_1wr.v" \
         "$ROOT/ware/rtl/cgen_memory_2rd_1wr.v" "ware/rtl/cgen_memory_2rd_1wr.v"; do
  [ -r "$c" ] && { MODEL="$c"; break; }
done
if [ -z "$MODEL" ]; then
  echo "SKIP-REASON: ware/rtl/cgen_memory_2rd_1wr.v not in runfiles"
  echo "SKIPPED: mem_multiread_hold_test -- no case executed. NOT acceptance."
  exit 0
fi
if ! command -v iverilog >/dev/null 2>&1 || ! command -v vvp >/dev/null 2>&1; then
  echo "SKIP-REASON: iverilog=$(command -v iverilog || echo MISSING) vvp=$(command -v vvp || echo MISSING)"
  echo "SKIPPED: mem_multiread_hold_test -- no case executed. NOT acceptance."
  exit 0
fi

W="${TEST_TMPDIR:-$ROOT/generated/tests}/mem_multiread_hold"
rm -rf "$W"; mkdir -p "$W"
rc_all=0

# INIT packs word i at [i*8 +: 8], so word0=0x11 ... word7=0x88.
cat > "$W/tb.v" <<'EOF'
`timescale 1ns/1ps
module tb;
  reg clk = 0; integer errs = 0;
  reg  [2:0] a0 = 0, a1 = 0, wa = 0;
  reg        e0 = 0, e1 = 0, we = 0;
  reg  [7:0] wd = 0;
  wire [7:0] d0, d1, f0, f1;
  always #5 clk = ~clk;

  // FWD=0: no same-cycle forwarding, so the hold cases are not perturbed.
  cgen_memory_2rd_1wr #(.BITS(8), .SIZE(8), .WENSIZE(1), .FWD(0), .LATENCY_0(1),
                        .INIT_EN(1), .INIT(64'h8877665544332211)) u (
    .clk(clk), .rd_addr_0(a0), .rd_enable_0(e0), .rd_dout_0(d0),
               .rd_addr_1(a1), .rd_enable_1(e1), .rd_dout_1(d1),
               .wr_addr_0(wa), .wr_enable_0(we), .wr_din_0(wd));
  // FWD=3: both read ports forward write port 0 (bit k*n_wr+j).
  cgen_memory_2rd_1wr #(.BITS(8), .SIZE(8), .WENSIZE(1), .FWD(3), .LATENCY_0(1),
                        .INIT_EN(1), .INIT(64'h8877665544332211)) uf (
    .clk(clk), .rd_addr_0(a0), .rd_enable_0(e0), .rd_dout_0(f0),
               .rd_addr_1(a1), .rd_enable_1(e1), .rd_dout_1(f1),
               .wr_addr_0(wa), .wr_enable_0(we), .wr_din_0(wd));

  task step(input [2:0] A0, input E0, input [2:0] A1, input E1,
            input W, input [2:0] WA, input [7:0] WD);
    begin
      @(negedge clk); a0=A0; e0=E0; a1=A1; e1=E1; we=W; wa=WA; wd=WD;
      @(posedge clk); #1;
    end
  endtask
  task chk(input [8*24:1] nm, input [7:0] got, input [7:0] want);
    begin
      if (got !== want) begin
        errs = errs + 1;
        $display("MISMATCH %0s: got %02h want %02h", nm, got, want);
      end
    end
  endtask

  initial begin
    // 1. both enabled: establish known values.
    step(3'd0,1,3'd1,1, 0,0,0);  chk("c1 port0", d0, 8'h11); chk("c1 port1", d1, 8'h22);
    // 2. port0 DISABLED while BOTH addresses change; port1 stays enabled.
    //    port0 must HOLD 0x11; port1 must read word4 = 0x55.
    //    Gating port1 with port0's enable would make port1 hold 0x22 here.
    step(3'd3,0,3'd4,1, 0,0,0);  chk("c2 port0 hold", d0, 8'h11); chk("c2 port1 read", d1, 8'h55);
    // 3. the opposite combination.
    //    port0 reads word5 = 0x66; port1 must HOLD 0x55.
    //    Gating port1 with port0's enable would make port1 read 0x77 here.
    step(3'd5,1,3'd6,0, 0,0,0);  chk("c3 port0 read", d0, 8'h66); chk("c3 port1 hold", d1, 8'h55);
    // 4. both disabled, addresses change: both hold.
    step(3'd7,0,3'd0,0, 0,0,0);  chk("c4 port0 hold", d0, 8'h66); chk("c4 port1 hold", d1, 8'h55);
    // 5. re-enable both: both take the new addresses.
    step(3'd7,1,3'd0,1, 0,0,0);  chk("c5 port0 reenable", d0, 8'h88); chk("c5 port1 reenable", d1, 8'h11);
    // 6. ENABLED read/write collision, FWD=1 for both ports: a same-cycle
    //    write to the address being read forwards. FWD=0 reads the committed
    //    (old) word. Both instances see the same stimulus.
    step(3'd2,1,3'd2,1, 1,3'd2,8'hAA);
    chk("c6 fwd port0", f0, 8'hAA); chk("c6 fwd port1", f1, 8'hAA);
    chk("c6 nofwd port0", d0, 8'h33); chk("c6 nofwd port1", d1, 8'h33);
    // 7. the write committed, so a later enabled read sees it on both.
    step(3'd2,1,3'd2,1, 0,0,0);
    chk("c7 committed port0", d0, 8'hAA); chk("c7 committed port1", d1, 8'hAA);
    if (errs == 0) $display("MULTIREAD_OK"); else $display("MULTIREAD_FAIL errs=%0d", errs);
    $finish;
  end
endmodule
EOF

run_tb() {  # -> OUT / RC
  iverilog -g2012 -o "$W/tb.vvp" "$1" "$W/tb.v" >"$W/iv.log" 2>&1 || {
    OUT="$(tail -5 "$W/iv.log")"; RC=99; return; }
  OUT="$(vvp "$W/tb.vvp" 2>&1)"; RC=$?
}

echo "--- the shipped model must satisfy every case ---"
run_tb "$MODEL"
echo "$OUT" | sed 's/^/    /'
echo "    (vvp exit=$RC)"
if [ "$RC" -ne 0 ] || ! echo "$OUT" | grep -q "MULTIREAD_OK"; then
  echo "FAIL: the shipped cgen_memory_2rd_1wr does not meet the hold contract"
  rc_all=1
else
  echo "ok: per-port hold, re-enable, and FWD/no-FWD collision all exact"
fi

# --- negative controls: each must FAIL SEMANTICALLY, not fail to build ------
neg() {  # <name> <sed-program> <expect-substring>
  sed "$2" "$MODEL" > "$W/mut.v"
  if cmp -s "$W/mut.v" "$MODEL"; then
    echo "FAIL: mutation '$1' did not apply -- the control proves nothing"; rc_all=1; return; fi
  run_tb "$W/mut.v"
  if [ "$RC" -eq 99 ]; then
    echo "FAIL: mutation '$1' did not BUILD; a build failure is not a semantic kill"
    echo "$OUT" | sed 's/^/      /'; rc_all=1; return; fi
  if echo "$OUT" | grep -q "MULTIREAD_OK"; then
    echo "FAIL: mutation '$1' SURVIVED -- the test cannot see it"; rc_all=1; return; fi
  echo "ok: '$1' killed semantically (vvp exit=$RC) -- $(echo "$OUT" | grep -m1 MISMATCH)"
  echo "$OUT" | grep -q "$3" \
    || { echo "FAIL: '$1' failed, but not at the expected case ($3)"; rc_all=1; }
}
echo "--- negative controls ---"
neg "port 1 update made unconditional" \
    's/^\(\s*\)if (rd_enable_1) begin$/\1if (1) begin/' "c3 port1 hold"
neg "port 1 gated by port 0's enable" \
    's/^\(\s*\)if (rd_enable_1) begin$/\1if (rd_enable_0) begin/' "port1"

[ "$rc_all" -eq 0 ] || { echo "FAIL: mem_multiread_hold_test"; exit 1; }
echo "PASS: mem_multiread_hold_test (7 cases + 2 negative controls, all EXECUTED)"
