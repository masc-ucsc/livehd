#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A memory whose depth is not a power of two compiles through every reader, and through the yosys
# readers its accesses past the last entry keep their Verilog meaning.
#
# pass.bitwidth refuses a Memory port address it cannot prove inside [0, size)
# (mem-address-overflow). The yosys reader gave the cell the decoded index: 4 bits for a 12-entry
# array, so [0..15]. It already dropped a write past the end (enable gated) and returned 0 for a
# read past the end (a zero mux after the data), but the address itself was unproven, so every such
# design (Vortex's 12-entry ipdom stack) stopped with that error. The cell now gets
# `index < depth ? index : 0`, the clamp the slang reader generates and bitwidth recognizes.
# Low-lane narrowing (pass/cprop/cprop_lowlane.cpp, run before bitwidth) split that clamp over the
# fields of a concatenated address ({ptr, wid}) into an Or of two lane muxes, which bitwidth bounds
# only by [0..15], for both readers; it now leaves a Mux that gives a Memory its address alone.
#
#   * mem_past_depth.sv (a 12-entry [0:11] array and a 12-entry [3:14] array, both with 4-bit
#     addresses, and a 12-entry array addressed by {ptr, wid}) compiles with each yosys reader
#     (yosys-slang, yosys-verilog) and with the native slang reader, with no
#     mem-address-overflow;
#   * lhd lec (effort proof) proves each netlist equivalent to mem_past_depth_ref.sv, the same
#     design with every access guarded by hand (a write past the end not made, a read past the
#     end 0); a reference whose write past the end lands on entry 0, and one whose read past the
#     end returns entry 0, are each REFUTED;
#   * with LHD_EXTERNAL_SIM=1, each netlist equals the reference under iverilog on every cycle of
#     a 4000-cycle random run (writes and reads past the end included), and equals the source on
#     every read inside the array.
set -u
W="${TEST_TMPDIR:-/tmp/yosys_mem_past_depth_$$}"
mkdir -p "$W"
LHD="${LHD:-$PWD/lhd/lhd}"
[[ -x "$LHD" ]] || LHD="$PWD/bazel-bin/lhd/lhd"
T=inou/yosys/tests
fail() { echo "FAIL: $*" >&2; exit 1; }

READERS="yosys-slang yosys-verilog"
for r in $READERS; do
  "$LHD" compile verilog "$T/mem_past_depth.sv" --reader "$r" --top mem_past_depth --workdir "$W/w_$r" \
    --emit "verilog:$W/net_$r.v" >"$W/compile_$r.log" 2>&1 \
    || { tail -20 "$W/compile_$r.log" >&2; fail "mem_past_depth.sv did not compile with --reader $r"; }
  ! grep -q 'mem-address-overflow' "$W/compile_$r.log" \
    || { grep 'mem-address-overflow' "$W/compile_$r.log" >&2; fail "$r: an unproven memory address"; }
  echo "ok: $r: compiles, every memory address proven inside the array"

  "$LHD" lec --impl "verilog:$W/net_$r.v" --ref "verilog:$T/mem_past_depth_ref.sv" --top mem_past_depth \
    --workdir "$W/lec_$r" --set formal.effort=proof -q >"$W/lec_$r.log" 2>&1 \
    || { cat "$W/lec_$r.log" >&2; fail "$r: the netlist is not proven equivalent to the guarded reference"; }
  grep -q "'mem_past_depth' PROVEN" "$W/lec_$r.log" || { cat "$W/lec_$r.log" >&2; fail "$r: lec did not report PROVEN"; }
  echo "ok: $r: proven equivalent to the guarded reference"
done

# The native slang reader clamps the same way (upass_tolg clamp_mem_addr). A read past the end
# returns the clamped entry there (a value X permits), not 0, so only the compile is checked.
"$LHD" compile verilog "$T/mem_past_depth.sv" --reader slang --top mem_past_depth --workdir "$W/w_slang" \
  --emit "verilog:$W/net_slang.v" >"$W/compile_slang.log" 2>&1 \
  || { tail -20 "$W/compile_slang.log" >&2; fail "mem_past_depth.sv did not compile with --reader slang"; }
echo "ok: slang: compiles, every memory address proven inside the array"

# Controls: the two ways the clamp could change a value. A reference whose write past the end lands
# on entry 0 (the clamp without the enable gate), and one whose read past the end returns entry 0
# (the clamp without the zero mux), must each be REFUTED (lhd lec exit 10).
sed 's/if (we \&\& wa < 4.d12) m\[wa\] <= wd;/if (we) m[(wa < 4'"'"'d12) ? wa : 4'"'"'d0] <= wd;/' \
  "$T/mem_past_depth_ref.sv" >"$W/bad_wr.sv"
sed 's/? m\[ra\] : 8.d0;/? m[ra] : m[0];/' "$T/mem_past_depth_ref.sv" >"$W/bad_rd.sv"
grep -q "m\[(wa < 4'd12) ? wa : 4'd0\] <= wd" "$W/bad_wr.sv" || fail "could not make the aliasing-write control"
grep -q '? m\[ra\] : m\[0\];' "$W/bad_rd.sv" || fail "could not make the entry-0-read control"
for b in bad_wr bad_rd; do
  code=0
  "$LHD" lec --impl "verilog:$W/net_yosys-slang.v" --ref "verilog:$W/$b.sv" --top mem_past_depth \
    --workdir "$W/lec_$b" --set formal.effort=proof -q >"$W/lec_$b.log" 2>&1 || code=$?
  [ "$code" = 10 ] && grep -q "'mem_past_depth' REFUTED" "$W/lec_$b.log" \
    || { cat "$W/lec_$b.log" >&2; fail "control $b: expected REFUTED (exit 10), got exit $code"; }
  echo "ok: control $b REFUTED"
done

if [ -z "${LHD_EXTERNAL_SIM:-}" ]; then
  echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
  echo "PASS: yosys_mem_past_depth_test (compile and lec legs)"
  exit 0
fi
command -v iverilog >/dev/null 2>&1 && command -v vvp >/dev/null 2>&1 \
  || fail "LHD_EXTERNAL_SIM is set but iverilog/vvp is not on PATH"
# the netlist `include`s the cgen memory primitive (ware/rtl/cgen_memory_<r>rd_<w>wr.v)
RTL_INC="ware/rtl"
[ -d "$RTL_INC" ] || RTL_INC="$(cd "$(dirname "$0")/../../.." && pwd)/ware/rtl"
[ -d "$RTL_INC" ] || fail "could not locate ware/rtl for the cgen memory primitives"
perl -pe 's/^module mem_past_depth\b/module mem_past_depth_s/' "$T/mem_past_depth.sv" >"$W/src_s.v"
cat >"$W/tb.v" <<'EOF'
module tb;
  reg clk = 0;
  reg we, we2, we3;
  reg [3:0] wa, ra, wa2, ra2;
  reg [1:0] wp, ww, rp, rw;
  reg [7:0] wd, wd2, wd3;
  wire [7:0] rd_r, rd2_r, rd3_r, rd_n, rd2_n, rd3_n, rd_s, rd2_s, rd3_s;
  mem_past_depth   gold(.clk(clk), .we(we), .wa(wa), .wd(wd), .ra(ra), .rd(rd_r),
                        .we2(we2), .wa2(wa2), .wd2(wd2), .ra2(ra2), .rd2(rd2_r),
                        .we3(we3), .wp(wp), .ww(ww), .wd3(wd3), .rp(rp), .rw(rw), .rd3(rd3_r));
  mem_past_depth_n net(.clk(clk), .we(we), .wa(wa), .wd(wd), .ra(ra), .rd(rd_n),
                       .we2(we2), .wa2(wa2), .wd2(wd2), .ra2(ra2), .rd2(rd2_n),
                       .we3(we3), .wp(wp), .ww(ww), .wd3(wd3), .rp(rp), .rw(rw), .rd3(rd3_n));
  mem_past_depth_s src(.clk(clk), .we(we), .wa(wa), .wd(wd), .ra(ra), .rd(rd_s),
                       .we2(we2), .wa2(wa2), .wd2(wd2), .ra2(ra2), .rd2(rd2_s),
                       .we3(we3), .wp(wp), .ww(ww), .wd3(wd3), .rp(rp), .rw(rw), .rd3(rd3_s));
  integer i, seed = 7, errs = 0, src_errs = 0, oob_wr = 0, oob_rd = 0;
  wire ra_in = ra < 4'd12;
  wire ra2_in = ra2 >= 4'd3 && ra2 <= 4'd14;
  wire rp_in = rp != 2'd3;
  initial begin
    for (i = 0; i < 4000; i = i + 1) begin
      if (i < 16) begin
        // fill every entry first (writes 12..15 of m, 0..2 and 15 of o, and ptr 3 of s are past the end)
        we = 1; wa = i; wd = i * 7 + 1; we2 = 1; wa2 = i; wd2 = i * 5 + 3;
        we3 = 1; {wp, ww} = i; wd3 = i * 3 + 5;
      end else begin
        {we, wa, wd, we2, wa2, wd2} = $random(seed);
        {we3, wp, ww, wd3} = $random(seed);
      end
      {ra, ra2, rp, rw} = $random(seed);
      #1;
      if (we && wa >= 4'd12) oob_wr = oob_wr + 1;
      if (we2 && (wa2 < 4'd3 || wa2 > 4'd14)) oob_wr = oob_wr + 1;
      if (we3 && wp == 2'd3) oob_wr = oob_wr + 1;
      if (!ra_in) oob_rd = oob_rd + 1;
      if (!ra2_in) oob_rd = oob_rd + 1;
      if (!rp_in) oob_rd = oob_rd + 1;
      if ({rd_n, rd2_n, rd3_n} !== {rd_r, rd2_r, rd3_r}) begin
        errs = errs + 1;
        if (errs < 5) $display("MISMATCH cycle %0d ra=%0d ra2=%0d rp=%0d: net %h %h %h ref %h %h %h",
                               i, ra, ra2, rp, rd_n, rd2_n, rd3_n, rd_r, rd2_r, rd3_r);
      end
      if ((ra_in && rd_n !== rd_s) || (ra2_in && rd2_n !== rd2_s) || (rp_in && rd3_n !== rd3_s)) begin
        src_errs = src_errs + 1;
        if (src_errs < 5) $display("SRC MISMATCH cycle %0d ra=%0d ra2=%0d rp=%0d: net %h %h %h src %h %h %h",
                                   i, ra, ra2, rp, rd_n, rd2_n, rd3_n, rd_s, rd2_s, rd3_s);
      end
      clk = 1; #1; clk = 0;
    end
    if (errs == 0 && src_errs == 0)
      $display("AGREE %0d cycles (%0d writes and %0d reads past the end)", i, oob_wr, oob_rd);
    else $display("DISAGREE: %0d cycles differ from the reference, %0d in-range reads from the source", errs, src_errs);
    $finish;
  end
endmodule
EOF
for r in $READERS; do
  perl -pe 's/^module mem_past_depth\b/module mem_past_depth_n/' "$W/net_$r.v" >"$W/net_n_$r.v"
  grep -q 'module mem_past_depth_n' "$W/net_n_$r.v" || fail "$r: could not rename the netlist module"
  iverilog -g2012 -I "$RTL_INC" -o "$W/sim_$r" "$W/tb.v" "$T/mem_past_depth_ref.sv" "$W/src_s.v" "$W/net_n_$r.v" >"$W/iverilog_$r.log" 2>&1 \
    || { cat "$W/iverilog_$r.log" >&2; fail "$r: iverilog could not build the comparison"; }
  vvp -n "$W/sim_$r" >"$W/sim_$r.log" 2>&1
  grep -q '^AGREE' "$W/sim_$r.log" || { cat "$W/sim_$r.log" >&2; fail "$r: the netlist and the reference disagree under iverilog"; }
  echo "ok: $r: $(grep '^AGREE' "$W/sim_$r.log") under iverilog"
done
echo "PASS: yosys_mem_past_depth_test"
