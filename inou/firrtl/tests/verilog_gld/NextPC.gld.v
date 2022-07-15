module NextPC(
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
  wire  _GEN_1 = io_funct3 == 3'h6 ? io_inputx < io_inputy : io_funct3 == 3'h7 & io_inputx >= io_inputy; // @[]
  wire  _GEN_2 = io_funct3 == 3'h5 ? $signed(io_inputx) >= $signed(io_inputy) : _GEN_1; // @[]
  wire  _GEN_3 = io_funct3 == 3'h4 ? $signed(io_inputx) < $signed(io_inputy) : _GEN_2; // @[]
  wire  _GEN_4 = io_funct3 == 3'h1 ? io_inputx != io_inputy : _GEN_3; // @[]
  wire  _GEN_5 = io_funct3 == 3'h0 ? io_inputx == io_inputy : _GEN_4; // @[]
  wire [31:0] _T_17 = io_pc + io_imm;
  wire [31:0] _T_19 = io_pc + 32'h4;
  wire [31:0] _GEN_6 = io_taken ? _T_17 : _T_19; // @[]
  wire [31:0] _T_23 = io_inputx + io_imm;
  wire [31:0] _GEN_8 = io_jalr ? _T_23 : _T_19; // @[]
  wire [31:0] _GEN_10 = io_jal ? _T_17 : _GEN_8; // @[]
  assign io_nextpc = io_branch ? _GEN_6 : _GEN_10; // @[]
  assign io_taken = io_branch ? _GEN_5 : io_jal | io_jalr; // @[]
endmodule
