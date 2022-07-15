module ImmediateGenerator(
  input         clock,
  input         reset,
  input  [31:0] io_instruction,
  output [31:0] io_sextImm
);
  wire [6:0] opcode = io_instruction[6:0];
  wire [31:0] _T_3 = {io_instruction[31:12],12'h0};
  wire [19:0] _T_15 = {io_instruction[31],io_instruction[19:12],io_instruction[20],io_instruction[30:21]};
  wire [10:0] _T_18 = _T_15[19] ? 11'h7ff : 11'h0;
  wire [31:0] _T_20 = {_T_18,io_instruction[31],io_instruction[19:12],io_instruction[20],io_instruction[30:21],1'h0};
  wire [19:0] _T_25 = io_instruction[31] ? 20'hfffff : 20'h0;
  wire [31:0] _T_26 = {_T_25,io_instruction[31:20]};
  wire [11:0] _T_34 = {io_instruction[31],io_instruction[7],io_instruction[30:25],io_instruction[11:8]};
  wire [18:0] _T_37 = _T_34[11] ? 19'h7ffff : 19'h0;
  wire [31:0] _T_39 = {_T_37,io_instruction[31],io_instruction[7],io_instruction[30:25],io_instruction[11:8],1'h0};
  wire [11:0] _T_49 = {io_instruction[31:25],io_instruction[11:7]};
  wire [19:0] _T_52 = _T_49[11] ? 20'hfffff : 20'h0;
  wire [31:0] _T_53 = {_T_52,io_instruction[31:25],io_instruction[11:7]};
  wire [31:0] _T_63 = {27'h0,io_instruction[19:15]};
  wire [31:0] _GEN_0 = 7'h73 == opcode ? _T_63 : 32'h0; // @[]
  wire [31:0] _GEN_1 = 7'h13 == opcode ? _T_26 : _GEN_0; // @[]
  wire [31:0] _GEN_2 = 7'h23 == opcode ? _T_53 : _GEN_1; // @[]
  wire [31:0] _GEN_3 = 7'h3 == opcode ? _T_26 : _GEN_2; // @[]
  wire [31:0] _GEN_4 = 7'h63 == opcode ? _T_39 : _GEN_3; // @[]
  wire [31:0] _GEN_5 = 7'h67 == opcode ? _T_26 : _GEN_4; // @[]
  wire [31:0] _GEN_6 = 7'h6f == opcode ? _T_20 : _GEN_5; // @[]
  wire [31:0] _GEN_7 = 7'h17 == opcode ? _T_3 : _GEN_6; // @[]
  assign io_sextImm = 7'h37 == opcode ? _T_3 : _GEN_7; // @[]
endmodule
