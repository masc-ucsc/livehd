// Golden for a deep nested-ternary priority chain (count-leading-zeros).  This
// is the minimal distillation of the XiangShan reader-mismatch (LEC refuted)
// cluster: CLZ_28/29/30/31, lzc*, CMOUnit, RVCExpander, IntToFPDataModule,
// UIntToContLow0s, ShiftLeftPriorityWithLZDResult, ... (all attributed sl=OK
// yv=BAD by xs/attribute2.sh).
//
// The LiveHD `--reader yosys-verilog` path (lgyosys_tolg / proc / cgen)
// MISCOMPILES the terminal constant of a long nested-ternary chain: for io_in=0
// the correct count-leading-zeros is 10 (4'hA), but the yosys-verilog reader
// emits a corrupted constant (the XiangShan repro showed 4'hA->6, 4'h9->7), so
// its netlist is NOT logically equivalent to this source.  `--reader slang` and
// `--reader yosys-slang` lower it CORRECTLY (slang matches the source).  yosys's
// own `read_verilog` parses this file fine — the bug is purely in LiveHD's
// yosys-verilog front-end, so it is reproduced by the prp-yvr-clz_nest target
// (NOT by prp-equiv / prp-v2prp, which use the slang + pyrope path and PASS).
module \clz_nest.clz (
  input  [9:0] io_in,
  output [3:0] io_out
);
  assign io_out =
    io_in[9] ? 4'h0 : io_in[8] ? 4'h1 : io_in[7] ? 4'h2 : io_in[6] ? 4'h3 :
    io_in[5] ? 4'h4 : io_in[4] ? 4'h5 : io_in[3] ? 4'h6 : io_in[2] ? 4'h7 :
    io_in[1] ? 4'h8 : io_in[0] ? 4'h9 : 4'hA;
endmodule
