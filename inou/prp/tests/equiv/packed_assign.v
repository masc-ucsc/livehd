// Golden for a SystemVerilog `'{...}` assignment pattern initializing a packed
// 2-D array, then a runtime element select `g[sel]`.  This is the minimal
// distillation of the XiangShan `--reader yosys-verilog` READ-FAIL cluster:
// BlockCipherModule, DecodeUnitComp, HPerfCounter*, MissEntry, RRArbiterInit_3,
// Reduction, SRT16DividerDataModule, SRT4qdsCons, UopInfoGen, VFAlu,
// VecExcpDataMergeModule -- every one fails `read_verilog -sv` with
// `syntax error, unexpected OP_CAST` at the `'{...}` literal.
//
// yosys's `read_verilog` (and therefore LiveHD's `--reader yosys-verilog`,
// which uses it) CANNOT parse `'{...}` -- it tokenizes the `'` as a cast.
// `--reader slang` and `--reader yosys-slang` parse it fine.  So this is a
// yosys-front-end gap, not a correctness bug: reproduced by prp-yvr-packed_assign
// (the read fails, 0 graphs).  Excluded from prp-equiv / prp-v2prp because those
// LEC the golden via yosys read_verilog, which cannot read this file.
module \packed_assign.pa (
  input  [1:0] sel,
  output [3:0] z
);
  wire [3:0][3:0] g = '{4'h2, 4'h4, 4'h8, 4'h1};   // g[3]=2 g[2]=4 g[1]=8 g[0]=1
  assign z = g[sel];
endmodule
