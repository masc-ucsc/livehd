module NextPCSimple(
  input         clock,
  input         reset,
  input         io_branch,
  input         io_jal,
  input         io_jalr,
  input  [31:0] io_inputx,
  input  [31:0] io_inputy,
  input  [2:0]  io_funct3,
  input  [31:0] io_pc,
  input  [31:0] io_imm,
  output [31:0] io_nextpc,
  output        io_taken
);
  wire  _GEN_1 = io_funct3 == 3'h1 ? io_inputx != io_inputy : io_funct3 == 3'h4 & $signed(io_inputx) < $signed(io_inputy
    ); // @[]
  wire  _GEN_2 = io_funct3 == 3'h0 ? io_inputx == io_inputy : _GEN_1; // @[]
  assign io_nextpc = 32'h8;
  assign io_taken = io_branch & _GEN_2; // @[]
endmodule
