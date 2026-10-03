#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# SYNCHRONOUS READ ENABLE == CLOCK ENABLE ON THE OUTPUT REGISTER.
#
# yosys `$mem_v2` RD_EN on a sync read port is a clock enable: a disabled cycle
# HOLDS the read register. That is how the import records it
# (inou/yosys/lgyosys_tolg.cpp:2721 wires RD_EN straight to the port's enable
# pin), what pass.lean models (`sram_sync_read_reg_next ren raw cur =
# if ren then raw else cur`), and what pass.single_edge depends on when it
# folds a gated clock into that enable instead of keeping a gated clock.
#
# The Verilog support models did something else: they updated the register
# unconditionally from a read that is x while disabled, so a disabled cycle
# LOADED x -- 0 under the defined-input comparison -- rather than holding.
# Measured on txfmafrac_top: that is the difference between a normalized
# netlist and its pre-normalization twin on 473 of 3142 vectors.
#
# Cases: 1 hold across disabled cycles, 2 independent per-port enables,
#        3 the GENERATED template, 4 async unchanged, 5 forwarding unchanged.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." 2>/dev/null && pwd)"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
[ -x "$LHD" ] || LHD="lhd/lhd"
[ -x "$LHD" ] || { echo "FAIL: no lhd binary"; exit 1; }
DS=""
for c in "${TEST_SRCDIR:-}/_main/scripts/coreet_lowering_diffsim.py" \
         "$ROOT/scripts/coreet_lowering_diffsim.py" "scripts/coreet_lowering_diffsim.py"; do
  [ -r "$c" ] && { DS="$c"; break; }
done
W="${TEST_TMPDIR:-$ROOT/generated/tests}/mem_sync_read_hold"
rm -rf "$W"; mkdir -p "$W"
rc_all=0
fail() { echo "FAIL: $*"; rc_all=1; }

cat > "$W/d.v" <<'EOF'
module clkgate(input clk_i, input en_i, output clk_o);
  reg en_latch;
  always @* if (!clk_i) en_latch = en_i;
  assign clk_o = clk_i & en_latch;
endmodule
// A registered ROM read behind a clock gate: yosys proc_rom gives a memory
// with a SYNC read port, and single_edge folds the gate into its RD_EN.
// Values are NONZERO so a hold is distinguishable from a forced zero.
module memicg(input clk, input en, input [2:0] raddr,
              output reg [7:0] dout, output reg [7:0] shadow);
  wire gclk;
  clkgate u_cg(.clk_i(clk), .en_i(en), .clk_o(gclk));
  always @(posedge gclk) begin
    case (raddr)
      3'd0: dout <= 8'h11; 3'd1: dout <= 8'h22; 3'd2: dout <= 8'h33; 3'd3: dout <= 8'h44;
      3'd4: dout <= 8'h55; 3'd5: dout <= 8'h66; 3'd6: dout <= 8'h77; 3'd7: dout <= 8'h88;
    endcase
  end
  always @(posedge clk) shadow <= {5'b0, raddr};
endmodule
// TWO read ports with INDEPENDENT enables: disabling one must not disturb the
// other, which a single shared gate would hide.
module mem2rd(input clk, input ea, input eb, input [2:0] aa, input [2:0] ab,
              output reg [7:0] da, output reg [7:0] db, output reg [7:0] shadow);
  wire ga, gb;
  clkgate u_a(.clk_i(clk), .en_i(ea), .clk_o(ga));
  clkgate u_b(.clk_i(clk), .en_i(eb), .clk_o(gb));
  always @(posedge ga) begin
    case (aa)
      3'd0: da <= 8'h11; 3'd1: da <= 8'h22; 3'd2: da <= 8'h33; 3'd3: da <= 8'h44;
      3'd4: da <= 8'h55; 3'd5: da <= 8'h66; 3'd6: da <= 8'h77; 3'd7: da <= 8'h88;
    endcase
  end
  always @(posedge gb) begin
    case (ab)
      3'd0: db <= 8'h99; 3'd1: db <= 8'haa; 3'd2: db <= 8'hbb; 3'd3: db <= 8'hcc;
      3'd4: db <= 8'hdd; 3'd5: db <= 8'hee; 3'd6: db <= 8'hf1; 3'd7: db <= 8'hf2;
    endcase
  end
  always @(posedge clk) shadow <= {5'b0, aa};
endmodule
// ASYNC read with a write port: no output register, so the read-enable
// behaviour under test must NOT change here, and same-cycle forwarding and the
// lane mask must stay as they are.
module memasync(input clk, input we, input [2:0] wa, input [7:0] wd,
                input [2:0] ra, output [7:0] q);
  reg [7:0] m [0:7];
  always @(posedge clk) if (we) m[wa] <= wd;
  assign q = m[ra];
endmodule
EOF
echo "$W/d.v" > "$W/d.f"

build() { rm -rf "$W/lg_$1" "$W/cw_$1"
  "$LHD" compile verilog "$W/d.v" --top "$1" --reader yosys-verilog \
    --emit-dir "lg:$W/lg_$1" --workdir "$W/cw_$1" -q >"$W/c_$1.log" 2>&1; }
norm()  { rm -rf "$W/n_$1" "$W/pw_$1"
  "$LHD" pass single_edge --top "$1" "lg:$W/lg_$1" --emit-dir "lg:$W/n_$1" \
    --workdir "$W/pw_$1" >"$W/n_$1.log" 2>&1; }
emit()  { rm -rf "$W/ew_$3"; "$LHD" compile "lg:$1" --top "$3" --recipe O0 \
    --emit "verilog:$2" --workdir "$W/ew_$3" -q >"$W/e_$3.log" 2>&1; }
ds() {  # <top> <impl> <ref-flag> <ref> <tag>
  python3 "$DS" --top "$1" --impl "$2" "$3" "$4" --out "$W/ds_$5" \
    --vectors 64 --seed 11 2>&1; }

HAVE_DS=0
[ -n "$DS" ] && command -v verilator >/dev/null 2>&1 && HAVE_DS=1
if [ "$HAVE_DS" != 1 ]; then
  echo "SKIP-REASON: verilator=$(command -v verilator || echo MISSING) diffsim=${DS:-MISSING}"
  echo "SKIPPED: mem_sync_read_hold_test -- the semantic cases did NOT execute."
  echo "         This is NOT acceptance; the structural checks alone cannot"
  echo "         establish the hold contract."
  exit 0
fi

# --- 1. hold across disabled cycles ---------------------------------------
build memicg || fail "compile memicg"
emit "$W/lg_memicg" "$W/raw.v" memicg || fail "emit raw memicg"
grep -q "LATENCY_0(1)" "$W/raw.v" \
  || fail "the fixture did not produce a SYNCHRONOUS read port (no LATENCY_0(1)) -- it is not testing this contract"
norm memicg || fail "single_edge memicg"
emit "$W/n_memicg" "$W/norm.v" memicg || fail "emit norm memicg"
grep -qE "\.rd_enable_0\(and_" "$W/norm.v" \
  || fail "the gate was not folded into the read enable, so the normalization path is not exercised"
if [ "$HAVE_DS" = 1 ]; then
  for pair in "raw.v --filelist $W/d.f rawrtl" "norm.v --filelist $W/d.f normrtl"; do
    set -- $pair
    out="$(ds memicg "$W/$1" "$2" "$3" "$4")"
    echo "$out" | grep -q "DIFFSIM-PASS" \
      || { echo "$out" | tail -4 | sed 's/^/    /'; fail "memicg $4 vs original RTL"; }
  done
  out="$(ds memicg "$W/norm.v" --ref-netlist "$W/raw.v" nvr)"
  echo "$out" | grep -q "DIFFSIM-PASS" \
    || { echo "$out" | tail -5 | sed 's/^/    /'
         fail "normalized vs pre-normalized: the disabled read did not HOLD"; }
  echo "ok: a sync read holds across disabled cycles (norm == raw == RTL)"
fi

# --- 2. independent per-port enables --------------------------------------
if [ "$HAVE_DS" = 1 ]; then
  build mem2rd && emit "$W/lg_mem2rd" "$W/raw2.v" mem2rd && norm mem2rd \
    && emit "$W/n_mem2rd" "$W/norm2.v" mem2rd || fail "mem2rd setup"
  out="$(ds mem2rd "$W/norm2.v" --ref-netlist "$W/raw2.v" m2)"
  echo "$out" | grep -q "DIFFSIM-PASS" \
    || { echo "$out" | tail -5 | sed 's/^/    /'; fail "two read ports with independent enables disagree"; }
  # SCOPE, stated exactly: the two gated ROMs lower to two SINGLE-read
  # memories, so this covers two independent memory INSTANCES, each gated by
  # its own enable. It does NOT exercise rd_enable_1 versus rd_enable_0 inside
  # one multi-read model, which is where gating port 1 with port 0's enable
  # would show up. Getting a single array with two sync read ports through this
  # frontend did not work: yosys replaces a small array with registers, and a
  # 256-entry version emitted no cgen_memory instance at all. Recorded as a
  # coverage gap, not covered here.
  nen="$(grep -oE '\.rd_enable_0\([^)]*\)' "$W/norm2.v" | sort -u | wc -l)"
  [ "$nen" -ge 2 ] \
    || fail "the 2-read fixture produced $nen distinct read enable(s); it cannot show independence"
  echo "ok: per-port read enables are independent ($nen distinct)"
fi

# --- 3. BOTH model paths gate the registered read -------------------------
# The shipped ware/rtl wrapper and the template cgen generates for a (R,W,clock)
# shape ware/rtl does not ship are two separate copies of this behaviour; a fix
# to one and not the other leaves half the designs wrong.
WARE=""
for c in "$ROOT/ware/rtl/cgen_memory_1rd_1wr.v" "ware/rtl/cgen_memory_1rd_1wr.v"; do
  [ -r "$c" ] && { WARE="$c"; break; }
done
if [ -n "$WARE" ]; then
  # Scoped to the REGISTERED block: a bare `if (rd_enable_0)` also matches the
  # combinational read above it, so grepping the whole file would pass even
  # with the registered update left unconditional.
  lat="$(awk '/BLOCK_RD_LAT_0/{f=1} f{print} f&&/^  end else begin:BLOCK_RD_COMB_0/{exit}' "$WARE")"
  echo "$lat" | grep -qE "if \(rd_enable_0\)" \
    || fail "the shipped model's REGISTERED read block does not gate on rd_enable_0"
  grep -qE "assign rd_dout_0 = d0_fwd" "$WARE" \
    || fail "the shipped model lost its ASYNC path"
  echo "ok: STRUCTURAL -- the shipped model's registered block gates, async untouched"
else
  echo "note: ware/rtl not reachable from runfiles; shipped-model text not asserted"
fi
CG=""
for c in "$ROOT/inou/cgen/cgen_verilog.cpp" "inou/cgen/cgen_verilog.cpp"; do
  [ -r "$c" ] && { CG="$c"; break; }
done
if [ -n "$CG" ]; then
  grep -q '") if (rd_enable_"' "$CG" \
    || fail "the GENERATED model template does not gate its registered read"
  echo "ok: STRUCTURAL ONLY -- the generated template's text gates its registered"
  echo "    read. This is a source check, NOT a behavioural one: no design here"
  echo "    uses a shape ware/rtl does not ship, so the generated model is never"
  echo "    simulated. Coverage gap, recorded."
else
  echo "note: cgen_verilog.cpp not reachable; generated template not asserted"
fi

# --- 4/5. async + forwarding unchanged ------------------------------------
if [ "$HAVE_DS" = 1 ]; then
  build memasync && emit "$W/lg_memasync" "$W/rawa.v" memasync || fail "memasync setup"
  grep -q "LATENCY_0(0)" "$W/rawa.v" || fail "memasync is not an async read port"
  out="$(ds memasync "$W/rawa.v" --filelist "$W/d.f" async)"
  echo "$out" | grep -q "DIFFSIM-PASS" \
    || { echo "$out" | tail -4 | sed 's/^/    /'; fail "async read + write forwarding changed"; }
  echo "ok: the async-read + write fixture agrees with its RTL (random +"
  echo "    directed vectors). SCOPE: this shows the async path and this"
  echo "    design's write/read behaviour are unchanged. It does NOT enumerate"
  echo "    the FWD/UNDEF collision matrix; lhd/tests/mem_forward_test.sh and"
  echo "    mem_mixed_lean_diff_test.sh are the focused tests for those and are"
  echo "    run separately."
fi

[ "$rc_all" -eq 0 ] || { echo "FAIL: mem_sync_read_hold_test"; exit 1; }
echo "PASS: mem_sync_read_hold_test"
