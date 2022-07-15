module HazardUnitBP(
  input        clock,
  input        reset,
  input  [4:0] io_rs1,
  input  [4:0] io_rs2,
  input        io_id_prediction,
  input        io_idex_memread,
  input  [4:0] io_idex_rd,
  input        io_exmem_taken,
  output [1:0] io_pcSel,
  output       io_if_id_stall,
  output       io_if_id_flush,
  output       io_id_ex_flush,
  output       io_ex_mem_flush
);
  wire [1:0] _GEN_0 = io_id_prediction ? 2'h2 : 2'h0; // @[]
  wire [1:0] _GEN_3 = io_idex_memread & (io_idex_rd == io_rs1 | io_idex_rd == io_rs2) ? 2'h3 : _GEN_0; // @[]
  wire  _GEN_5 = io_idex_memread & (io_idex_rd == io_rs1 | io_idex_rd == io_rs2) ? 1'h0 : io_id_prediction; // @[]
  assign io_pcSel = io_exmem_taken ? 2'h1 : _GEN_3; // @[]
  assign io_if_id_stall = io_exmem_taken ? 1'h0 : io_idex_memread & (io_idex_rd == io_rs1 | io_idex_rd == io_rs2); // @[]
  assign io_if_id_flush = io_exmem_taken | _GEN_5; // @[]
  assign io_id_ex_flush = io_exmem_taken | io_idex_memread & (io_idex_rd == io_rs1 | io_idex_rd == io_rs2); // @[]
  assign io_ex_mem_flush = io_exmem_taken; // @[]
endmodule
