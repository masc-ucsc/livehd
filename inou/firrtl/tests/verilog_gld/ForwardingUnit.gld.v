module ForwardingUnit(
  input        clock,
  input        reset,
  input  [4:0] io_rs1,
  input  [4:0] io_rs2,
  input  [4:0] io_exmemrd,
  input        io_exmemrw,
  input  [4:0] io_memwbrd,
  input        io_memwbrw,
  output [1:0] io_forwardA,
  output [1:0] io_forwardB
);
  wire [1:0] _GEN_0 = io_memwbrw & io_memwbrd == io_rs1 & io_memwbrd != 5'h0 ? 2'h2 : 2'h0; // @[]
  wire [1:0] _GEN_2 = io_memwbrw & io_memwbrd == io_rs2 & io_memwbrd != 5'h0 ? 2'h2 : 2'h0; // @[]
  assign io_forwardA = io_exmemrw & io_exmemrd == io_rs1 & io_exmemrd != 5'h0 ? 2'h1 : _GEN_0; // @[]
  assign io_forwardB = io_exmemrw & io_exmemrd == io_rs2 & io_exmemrd != 5'h0 ? 2'h1 : _GEN_2; // @[]
endmodule
