module StageReg_2(
  input        clock,
  input        reset,
  input        io_in_ex_ctrl_itype,
  input        io_in_ex_ctrl_aluop,
  input        io_in_ex_ctrl_resultselect,
  input        io_in_ex_ctrl_xsrc,
  input        io_in_ex_ctrl_ysrc,
  input        io_in_ex_ctrl_plus4,
  input        io_in_ex_ctrl_branch,
  input        io_in_ex_ctrl_jal,
  input        io_in_ex_ctrl_jalr,
  input  [1:0] io_in_mem_ctrl_memop,
  input        io_in_wb_ctrl_toreg,
  input        io_in_wb_ctrl_regwrite,
  input        io_flush,
  output       io_data_ex_ctrl_itype,
  output       io_data_ex_ctrl_aluop,
  output       io_data_ex_ctrl_resultselect,
  output       io_data_ex_ctrl_xsrc,
  output       io_data_ex_ctrl_ysrc,
  output       io_data_ex_ctrl_plus4,
  output       io_data_ex_ctrl_branch,
  output       io_data_ex_ctrl_jal,
  output       io_data_ex_ctrl_jalr,
  output [1:0] io_data_mem_ctrl_memop,
  output       io_data_wb_ctrl_toreg,
  output       io_data_wb_ctrl_regwrite
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_5;
  reg [31:0] _RAND_6;
  reg [31:0] _RAND_7;
  reg [31:0] _RAND_8;
  reg [31:0] _RAND_9;
  reg [31:0] _RAND_10;
  reg [31:0] _RAND_11;
`endif // RANDOMIZE_REG_INIT
  reg  reg_ex_ctrl_itype;
  reg  reg_ex_ctrl_aluop;
  reg  reg_ex_ctrl_resultselect;
  reg  reg_ex_ctrl_xsrc;
  reg  reg_ex_ctrl_ysrc;
  reg  reg_ex_ctrl_plus4;
  reg  reg_ex_ctrl_branch;
  reg  reg_ex_ctrl_jal;
  reg  reg_ex_ctrl_jalr;
  reg [1:0] reg_mem_ctrl_memop;
  reg  reg_wb_ctrl_toreg;
  reg  reg_wb_ctrl_regwrite;
  assign io_data_ex_ctrl_itype = reg_ex_ctrl_itype;
  assign io_data_ex_ctrl_aluop = reg_ex_ctrl_aluop;
  assign io_data_ex_ctrl_resultselect = reg_ex_ctrl_resultselect;
  assign io_data_ex_ctrl_xsrc = reg_ex_ctrl_xsrc;
  assign io_data_ex_ctrl_ysrc = reg_ex_ctrl_ysrc;
  assign io_data_ex_ctrl_plus4 = reg_ex_ctrl_plus4;
  assign io_data_ex_ctrl_branch = reg_ex_ctrl_branch;
  assign io_data_ex_ctrl_jal = reg_ex_ctrl_jal;
  assign io_data_ex_ctrl_jalr = reg_ex_ctrl_jalr;
  assign io_data_mem_ctrl_memop = reg_mem_ctrl_memop;
  assign io_data_wb_ctrl_toreg = reg_wb_ctrl_toreg;
  assign io_data_wb_ctrl_regwrite = reg_wb_ctrl_regwrite;
  always @(posedge clock) begin
    if (reset) begin
      reg_ex_ctrl_itype <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_itype <= 1'h0;
    end else begin
      reg_ex_ctrl_itype <= io_in_ex_ctrl_itype;
    end
    if (reset) begin
      reg_ex_ctrl_aluop <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_aluop <= 1'h0;
    end else begin
      reg_ex_ctrl_aluop <= io_in_ex_ctrl_aluop;
    end
    if (reset) begin
      reg_ex_ctrl_resultselect <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_resultselect <= 1'h0;
    end else begin
      reg_ex_ctrl_resultselect <= io_in_ex_ctrl_resultselect;
    end
    if (reset) begin
      reg_ex_ctrl_xsrc <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_xsrc <= 1'h0;
    end else begin
      reg_ex_ctrl_xsrc <= io_in_ex_ctrl_xsrc;
    end
    if (reset) begin
      reg_ex_ctrl_ysrc <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_ysrc <= 1'h0;
    end else begin
      reg_ex_ctrl_ysrc <= io_in_ex_ctrl_ysrc;
    end
    if (reset) begin
      reg_ex_ctrl_plus4 <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_plus4 <= 1'h0;
    end else begin
      reg_ex_ctrl_plus4 <= io_in_ex_ctrl_plus4;
    end
    if (reset) begin
      reg_ex_ctrl_branch <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_branch <= 1'h0;
    end else begin
      reg_ex_ctrl_branch <= io_in_ex_ctrl_branch;
    end
    if (reset) begin
      reg_ex_ctrl_jal <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_jal <= 1'h0;
    end else begin
      reg_ex_ctrl_jal <= io_in_ex_ctrl_jal;
    end
    if (reset) begin
      reg_ex_ctrl_jalr <= 1'h0;
    end else if (io_flush) begin
      reg_ex_ctrl_jalr <= 1'h0;
    end else begin
      reg_ex_ctrl_jalr <= io_in_ex_ctrl_jalr;
    end
    if (reset) begin
      reg_mem_ctrl_memop <= 2'h0;
    end else if (io_flush) begin
      reg_mem_ctrl_memop <= 2'h0;
    end else begin
      reg_mem_ctrl_memop <= io_in_mem_ctrl_memop;
    end
    if (reset) begin
      reg_wb_ctrl_toreg <= 1'h0;
    end else if (io_flush) begin
      reg_wb_ctrl_toreg <= 1'h0;
    end else begin
      reg_wb_ctrl_toreg <= io_in_wb_ctrl_toreg;
    end
    if (reset) begin
      reg_wb_ctrl_regwrite <= 1'h0;
    end else if (io_flush) begin
      reg_wb_ctrl_regwrite <= 1'h0;
    end else begin
      reg_wb_ctrl_regwrite <= io_in_wb_ctrl_regwrite;
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  reg_ex_ctrl_itype = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  reg_ex_ctrl_aluop = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  reg_ex_ctrl_resultselect = _RAND_2[0:0];
  _RAND_3 = {1{`RANDOM}};
  reg_ex_ctrl_xsrc = _RAND_3[0:0];
  _RAND_4 = {1{`RANDOM}};
  reg_ex_ctrl_ysrc = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  reg_ex_ctrl_plus4 = _RAND_5[0:0];
  _RAND_6 = {1{`RANDOM}};
  reg_ex_ctrl_branch = _RAND_6[0:0];
  _RAND_7 = {1{`RANDOM}};
  reg_ex_ctrl_jal = _RAND_7[0:0];
  _RAND_8 = {1{`RANDOM}};
  reg_ex_ctrl_jalr = _RAND_8[0:0];
  _RAND_9 = {1{`RANDOM}};
  reg_mem_ctrl_memop = _RAND_9[1:0];
  _RAND_10 = {1{`RANDOM}};
  reg_wb_ctrl_toreg = _RAND_10[0:0];
  _RAND_11 = {1{`RANDOM}};
  reg_wb_ctrl_regwrite = _RAND_11[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module StageReg_4(
  input        clock,
  input        reset,
  input  [1:0] io_in_mem_ctrl_memop,
  input        io_in_wb_ctrl_toreg,
  input        io_in_wb_ctrl_regwrite,
  input        io_flush,
  output [1:0] io_data_mem_ctrl_memop,
  output       io_data_wb_ctrl_toreg,
  output       io_data_wb_ctrl_regwrite
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
`endif // RANDOMIZE_REG_INIT
  reg [1:0] reg_mem_ctrl_memop;
  reg  reg_wb_ctrl_toreg;
  reg  reg_wb_ctrl_regwrite;
  assign io_data_mem_ctrl_memop = reg_mem_ctrl_memop;
  assign io_data_wb_ctrl_toreg = reg_wb_ctrl_toreg;
  assign io_data_wb_ctrl_regwrite = reg_wb_ctrl_regwrite;
  always @(posedge clock) begin
    if (reset) begin
      reg_mem_ctrl_memop <= 2'h0;
    end else if (io_flush) begin
      reg_mem_ctrl_memop <= 2'h0;
    end else begin
      reg_mem_ctrl_memop <= io_in_mem_ctrl_memop;
    end
    if (reset) begin
      reg_wb_ctrl_toreg <= 1'h0;
    end else if (io_flush) begin
      reg_wb_ctrl_toreg <= 1'h0;
    end else begin
      reg_wb_ctrl_toreg <= io_in_wb_ctrl_toreg;
    end
    if (reset) begin
      reg_wb_ctrl_regwrite <= 1'h0;
    end else if (io_flush) begin
      reg_wb_ctrl_regwrite <= 1'h0;
    end else begin
      reg_wb_ctrl_regwrite <= io_in_wb_ctrl_regwrite;
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  reg_mem_ctrl_memop = _RAND_0[1:0];
  _RAND_1 = {1{`RANDOM}};
  reg_wb_ctrl_toreg = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  reg_wb_ctrl_regwrite = _RAND_2[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module Control(
  input  [6:0] io_opcode,
  output       io_itype,
  output       io_aluop,
  output       io_xsrc,
  output       io_ysrc,
  output       io_branch,
  output       io_jal,
  output       io_jalr,
  output       io_plus4,
  output       io_resultselect,
  output [1:0] io_memop,
  output       io_toreg,
  output       io_regwrite
);
  wire  _T_37 = 7'h37 == io_opcode ? 1'h0 : 7'h17 == io_opcode | (7'h6f == io_opcode | 7'h67 == io_opcode);
  wire  _T_38 = 7'h63 == io_opcode ? 1'h0 : _T_37;
  wire  _T_39 = 7'h23 == io_opcode ? 1'h0 : _T_38;
  wire  _T_40 = 7'h3 == io_opcode ? 1'h0 : _T_39;
  wire  _T_41 = 7'h13 == io_opcode ? 1'h0 : _T_40;
  wire  _T_45 = 7'h37 == io_opcode ? 1'h0 : 7'h17 == io_opcode;
  wire  _T_46 = 7'h63 == io_opcode ? 1'h0 : _T_45;
  wire  _T_55 = 7'h23 == io_opcode ? 1'h0 : 7'h63 == io_opcode;
  wire  _T_56 = 7'h3 == io_opcode ? 1'h0 : _T_55;
  wire  _T_57 = 7'h13 == io_opcode ? 1'h0 : _T_56;
  wire  _T_60 = 7'h17 == io_opcode ? 1'h0 : 7'h6f == io_opcode;
  wire  _T_61 = 7'h37 == io_opcode ? 1'h0 : _T_60;
  wire  _T_62 = 7'h63 == io_opcode ? 1'h0 : _T_61;
  wire  _T_63 = 7'h23 == io_opcode ? 1'h0 : _T_62;
  wire  _T_64 = 7'h3 == io_opcode ? 1'h0 : _T_63;
  wire  _T_65 = 7'h13 == io_opcode ? 1'h0 : _T_64;
  wire  _T_67 = 7'h6f == io_opcode ? 1'h0 : 7'h67 == io_opcode;
  wire  _T_68 = 7'h17 == io_opcode ? 1'h0 : _T_67;
  wire  _T_69 = 7'h37 == io_opcode ? 1'h0 : _T_68;
  wire  _T_70 = 7'h63 == io_opcode ? 1'h0 : _T_69;
  wire  _T_71 = 7'h23 == io_opcode ? 1'h0 : _T_70;
  wire  _T_72 = 7'h3 == io_opcode ? 1'h0 : _T_71;
  wire  _T_73 = 7'h13 == io_opcode ? 1'h0 : _T_72;
  wire  _T_76 = 7'h17 == io_opcode ? 1'h0 : 7'h6f == io_opcode | 7'h67 == io_opcode;
  wire  _T_77 = 7'h37 == io_opcode ? 1'h0 : _T_76;
  wire  _T_78 = 7'h63 == io_opcode ? 1'h0 : _T_77;
  wire  _T_79 = 7'h23 == io_opcode ? 1'h0 : _T_78;
  wire  _T_80 = 7'h3 == io_opcode ? 1'h0 : _T_79;
  wire  _T_81 = 7'h13 == io_opcode ? 1'h0 : _T_80;
  wire  _T_86 = 7'h63 == io_opcode ? 1'h0 : 7'h37 == io_opcode;
  wire  _T_87 = 7'h23 == io_opcode ? 1'h0 : _T_86;
  wire  _T_88 = 7'h3 == io_opcode ? 1'h0 : _T_87;
  wire  _T_89 = 7'h13 == io_opcode ? 1'h0 : _T_88;
  wire [1:0] _T_95 = 7'h23 == io_opcode ? 2'h3 : 2'h0;
  wire [1:0] _T_96 = 7'h3 == io_opcode ? 2'h2 : _T_95;
  wire [1:0] _T_97 = 7'h13 == io_opcode ? 2'h0 : _T_96;
  wire  _T_105 = 7'h13 == io_opcode ? 1'h0 : 7'h3 == io_opcode;
  wire  _T_110 = 7'h63 == io_opcode ? 1'h0 : 7'h37 == io_opcode | (7'h17 == io_opcode | (7'h6f == io_opcode | 7'h67 ==
    io_opcode));
  wire  _T_111 = 7'h23 == io_opcode ? 1'h0 : _T_110;
  assign io_itype = 7'h33 == io_opcode ? 1'h0 : 7'h13 == io_opcode;
  assign io_aluop = 7'h33 == io_opcode | 7'h13 == io_opcode;
  assign io_xsrc = 7'h33 == io_opcode ? 1'h0 : _T_41;
  assign io_ysrc = 7'h33 == io_opcode ? 1'h0 : 7'h13 == io_opcode | (7'h3 == io_opcode | (7'h23 == io_opcode | _T_46));
  assign io_branch = 7'h33 == io_opcode ? 1'h0 : _T_57;
  assign io_jal = 7'h33 == io_opcode ? 1'h0 : _T_65;
  assign io_jalr = 7'h33 == io_opcode ? 1'h0 : _T_73;
  assign io_plus4 = 7'h33 == io_opcode ? 1'h0 : _T_81;
  assign io_resultselect = 7'h33 == io_opcode ? 1'h0 : _T_89;
  assign io_memop = 7'h33 == io_opcode ? 2'h0 : _T_97;
  assign io_toreg = 7'h33 == io_opcode ? 1'h0 : _T_105;
  assign io_regwrite = 7'h33 == io_opcode | (7'h13 == io_opcode | (7'h3 == io_opcode | _T_111));
endmodule
module Adder(
  input  [31:0] io_inputx,
  input  [31:0] io_inputy,
  output [31:0] io_result
);
  assign io_result = io_inputx + io_inputy;
endmodule
module ForwardingUnit(
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
module NextPC(
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
module RegisterFile(
  input         clock,
  input  [4:0]  io_readreg1,
  input  [4:0]  io_readreg2,
  input  [4:0]  io_writereg,
  input  [31:0] io_writedata,
  input         io_wen,
  output [31:0] io_readdata1,
  output [31:0] io_readdata2
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_5;
  reg [31:0] _RAND_6;
  reg [31:0] _RAND_7;
  reg [31:0] _RAND_8;
  reg [31:0] _RAND_9;
  reg [31:0] _RAND_10;
  reg [31:0] _RAND_11;
  reg [31:0] _RAND_12;
  reg [31:0] _RAND_13;
  reg [31:0] _RAND_14;
  reg [31:0] _RAND_15;
  reg [31:0] _RAND_16;
  reg [31:0] _RAND_17;
  reg [31:0] _RAND_18;
  reg [31:0] _RAND_19;
  reg [31:0] _RAND_20;
  reg [31:0] _RAND_21;
  reg [31:0] _RAND_22;
  reg [31:0] _RAND_23;
  reg [31:0] _RAND_24;
  reg [31:0] _RAND_25;
  reg [31:0] _RAND_26;
  reg [31:0] _RAND_27;
  reg [31:0] _RAND_28;
  reg [31:0] _RAND_29;
  reg [31:0] _RAND_30;
  reg [31:0] _RAND_31;
`endif // RANDOMIZE_REG_INIT
  reg [31:0] regs_0;
  reg [31:0] regs_1;
  reg [31:0] regs_2;
  reg [31:0] regs_3;
  reg [31:0] regs_4;
  reg [31:0] regs_5;
  reg [31:0] regs_6;
  reg [31:0] regs_7;
  reg [31:0] regs_8;
  reg [31:0] regs_9;
  reg [31:0] regs_10;
  reg [31:0] regs_11;
  reg [31:0] regs_12;
  reg [31:0] regs_13;
  reg [31:0] regs_14;
  reg [31:0] regs_15;
  reg [31:0] regs_16;
  reg [31:0] regs_17;
  reg [31:0] regs_18;
  reg [31:0] regs_19;
  reg [31:0] regs_20;
  reg [31:0] regs_21;
  reg [31:0] regs_22;
  reg [31:0] regs_23;
  reg [31:0] regs_24;
  reg [31:0] regs_25;
  reg [31:0] regs_26;
  reg [31:0] regs_27;
  reg [31:0] regs_28;
  reg [31:0] regs_29;
  reg [31:0] regs_30;
  reg [31:0] regs_31;
  wire [31:0] _GEN_65 = 5'h1 == io_readreg1 ? regs_1 : regs_0; // @[]
  wire [31:0] _GEN_66 = 5'h2 == io_readreg1 ? regs_2 : _GEN_65; // @[]
  wire [31:0] _GEN_67 = 5'h3 == io_readreg1 ? regs_3 : _GEN_66; // @[]
  wire [31:0] _GEN_68 = 5'h4 == io_readreg1 ? regs_4 : _GEN_67; // @[]
  wire [31:0] _GEN_69 = 5'h5 == io_readreg1 ? regs_5 : _GEN_68; // @[]
  wire [31:0] _GEN_70 = 5'h6 == io_readreg1 ? regs_6 : _GEN_69; // @[]
  wire [31:0] _GEN_71 = 5'h7 == io_readreg1 ? regs_7 : _GEN_70; // @[]
  wire [31:0] _GEN_72 = 5'h8 == io_readreg1 ? regs_8 : _GEN_71; // @[]
  wire [31:0] _GEN_73 = 5'h9 == io_readreg1 ? regs_9 : _GEN_72; // @[]
  wire [31:0] _GEN_74 = 5'ha == io_readreg1 ? regs_10 : _GEN_73; // @[]
  wire [31:0] _GEN_75 = 5'hb == io_readreg1 ? regs_11 : _GEN_74; // @[]
  wire [31:0] _GEN_76 = 5'hc == io_readreg1 ? regs_12 : _GEN_75; // @[]
  wire [31:0] _GEN_77 = 5'hd == io_readreg1 ? regs_13 : _GEN_76; // @[]
  wire [31:0] _GEN_78 = 5'he == io_readreg1 ? regs_14 : _GEN_77; // @[]
  wire [31:0] _GEN_79 = 5'hf == io_readreg1 ? regs_15 : _GEN_78; // @[]
  wire [31:0] _GEN_80 = 5'h10 == io_readreg1 ? regs_16 : _GEN_79; // @[]
  wire [31:0] _GEN_81 = 5'h11 == io_readreg1 ? regs_17 : _GEN_80; // @[]
  wire [31:0] _GEN_82 = 5'h12 == io_readreg1 ? regs_18 : _GEN_81; // @[]
  wire [31:0] _GEN_83 = 5'h13 == io_readreg1 ? regs_19 : _GEN_82; // @[]
  wire [31:0] _GEN_84 = 5'h14 == io_readreg1 ? regs_20 : _GEN_83; // @[]
  wire [31:0] _GEN_85 = 5'h15 == io_readreg1 ? regs_21 : _GEN_84; // @[]
  wire [31:0] _GEN_86 = 5'h16 == io_readreg1 ? regs_22 : _GEN_85; // @[]
  wire [31:0] _GEN_87 = 5'h17 == io_readreg1 ? regs_23 : _GEN_86; // @[]
  wire [31:0] _GEN_88 = 5'h18 == io_readreg1 ? regs_24 : _GEN_87; // @[]
  wire [31:0] _GEN_89 = 5'h19 == io_readreg1 ? regs_25 : _GEN_88; // @[]
  wire [31:0] _GEN_90 = 5'h1a == io_readreg1 ? regs_26 : _GEN_89; // @[]
  wire [31:0] _GEN_91 = 5'h1b == io_readreg1 ? regs_27 : _GEN_90; // @[]
  wire [31:0] _GEN_92 = 5'h1c == io_readreg1 ? regs_28 : _GEN_91; // @[]
  wire [31:0] _GEN_93 = 5'h1d == io_readreg1 ? regs_29 : _GEN_92; // @[]
  wire [31:0] _GEN_94 = 5'h1e == io_readreg1 ? regs_30 : _GEN_93; // @[]
  wire [31:0] _GEN_95 = 5'h1f == io_readreg1 ? regs_31 : _GEN_94; // @[]
  wire [31:0] _GEN_97 = 5'h1 == io_readreg2 ? regs_1 : regs_0; // @[]
  wire [31:0] _GEN_98 = 5'h2 == io_readreg2 ? regs_2 : _GEN_97; // @[]
  wire [31:0] _GEN_99 = 5'h3 == io_readreg2 ? regs_3 : _GEN_98; // @[]
  wire [31:0] _GEN_100 = 5'h4 == io_readreg2 ? regs_4 : _GEN_99; // @[]
  wire [31:0] _GEN_101 = 5'h5 == io_readreg2 ? regs_5 : _GEN_100; // @[]
  wire [31:0] _GEN_102 = 5'h6 == io_readreg2 ? regs_6 : _GEN_101; // @[]
  wire [31:0] _GEN_103 = 5'h7 == io_readreg2 ? regs_7 : _GEN_102; // @[]
  wire [31:0] _GEN_104 = 5'h8 == io_readreg2 ? regs_8 : _GEN_103; // @[]
  wire [31:0] _GEN_105 = 5'h9 == io_readreg2 ? regs_9 : _GEN_104; // @[]
  wire [31:0] _GEN_106 = 5'ha == io_readreg2 ? regs_10 : _GEN_105; // @[]
  wire [31:0] _GEN_107 = 5'hb == io_readreg2 ? regs_11 : _GEN_106; // @[]
  wire [31:0] _GEN_108 = 5'hc == io_readreg2 ? regs_12 : _GEN_107; // @[]
  wire [31:0] _GEN_109 = 5'hd == io_readreg2 ? regs_13 : _GEN_108; // @[]
  wire [31:0] _GEN_110 = 5'he == io_readreg2 ? regs_14 : _GEN_109; // @[]
  wire [31:0] _GEN_111 = 5'hf == io_readreg2 ? regs_15 : _GEN_110; // @[]
  wire [31:0] _GEN_112 = 5'h10 == io_readreg2 ? regs_16 : _GEN_111; // @[]
  wire [31:0] _GEN_113 = 5'h11 == io_readreg2 ? regs_17 : _GEN_112; // @[]
  wire [31:0] _GEN_114 = 5'h12 == io_readreg2 ? regs_18 : _GEN_113; // @[]
  wire [31:0] _GEN_115 = 5'h13 == io_readreg2 ? regs_19 : _GEN_114; // @[]
  wire [31:0] _GEN_116 = 5'h14 == io_readreg2 ? regs_20 : _GEN_115; // @[]
  wire [31:0] _GEN_117 = 5'h15 == io_readreg2 ? regs_21 : _GEN_116; // @[]
  wire [31:0] _GEN_118 = 5'h16 == io_readreg2 ? regs_22 : _GEN_117; // @[]
  wire [31:0] _GEN_119 = 5'h17 == io_readreg2 ? regs_23 : _GEN_118; // @[]
  wire [31:0] _GEN_120 = 5'h18 == io_readreg2 ? regs_24 : _GEN_119; // @[]
  wire [31:0] _GEN_121 = 5'h19 == io_readreg2 ? regs_25 : _GEN_120; // @[]
  wire [31:0] _GEN_122 = 5'h1a == io_readreg2 ? regs_26 : _GEN_121; // @[]
  wire [31:0] _GEN_123 = 5'h1b == io_readreg2 ? regs_27 : _GEN_122; // @[]
  wire [31:0] _GEN_124 = 5'h1c == io_readreg2 ? regs_28 : _GEN_123; // @[]
  wire [31:0] _GEN_125 = 5'h1d == io_readreg2 ? regs_29 : _GEN_124; // @[]
  wire [31:0] _GEN_126 = 5'h1e == io_readreg2 ? regs_30 : _GEN_125; // @[]
  wire [31:0] _GEN_127 = 5'h1f == io_readreg2 ? regs_31 : _GEN_126; // @[]
  wire [31:0] _GEN_128 = io_readreg2 == io_writereg & io_wen ? io_writedata : _GEN_127; // @[]
  assign io_readdata1 = io_readreg1 == io_writereg & io_wen ? io_writedata : _GEN_95; // @[]
  assign io_readdata2 = io_readreg1 == io_writereg & io_wen ? _GEN_127 : _GEN_128; // @[]
  always @(posedge clock) begin
    if (io_wen) begin
      if (5'h0 == io_writereg) begin
        regs_0 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h1 == io_writereg) begin
        regs_1 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h2 == io_writereg) begin
        regs_2 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h3 == io_writereg) begin
        regs_3 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h4 == io_writereg) begin
        regs_4 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h5 == io_writereg) begin
        regs_5 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h6 == io_writereg) begin
        regs_6 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h7 == io_writereg) begin
        regs_7 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h8 == io_writereg) begin
        regs_8 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h9 == io_writereg) begin
        regs_9 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'ha == io_writereg) begin
        regs_10 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'hb == io_writereg) begin
        regs_11 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'hc == io_writereg) begin
        regs_12 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'hd == io_writereg) begin
        regs_13 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'he == io_writereg) begin
        regs_14 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'hf == io_writereg) begin
        regs_15 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h10 == io_writereg) begin
        regs_16 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h11 == io_writereg) begin
        regs_17 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h12 == io_writereg) begin
        regs_18 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h13 == io_writereg) begin
        regs_19 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h14 == io_writereg) begin
        regs_20 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h15 == io_writereg) begin
        regs_21 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h16 == io_writereg) begin
        regs_22 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h17 == io_writereg) begin
        regs_23 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h18 == io_writereg) begin
        regs_24 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h19 == io_writereg) begin
        regs_25 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h1a == io_writereg) begin
        regs_26 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h1b == io_writereg) begin
        regs_27 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h1c == io_writereg) begin
        regs_28 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h1d == io_writereg) begin
        regs_29 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h1e == io_writereg) begin
        regs_30 <= io_writedata;
      end
    end
    if (io_wen) begin
      if (5'h1f == io_writereg) begin
        regs_31 <= io_writedata;
      end
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  regs_0 = _RAND_0[31:0];
  _RAND_1 = {1{`RANDOM}};
  regs_1 = _RAND_1[31:0];
  _RAND_2 = {1{`RANDOM}};
  regs_2 = _RAND_2[31:0];
  _RAND_3 = {1{`RANDOM}};
  regs_3 = _RAND_3[31:0];
  _RAND_4 = {1{`RANDOM}};
  regs_4 = _RAND_4[31:0];
  _RAND_5 = {1{`RANDOM}};
  regs_5 = _RAND_5[31:0];
  _RAND_6 = {1{`RANDOM}};
  regs_6 = _RAND_6[31:0];
  _RAND_7 = {1{`RANDOM}};
  regs_7 = _RAND_7[31:0];
  _RAND_8 = {1{`RANDOM}};
  regs_8 = _RAND_8[31:0];
  _RAND_9 = {1{`RANDOM}};
  regs_9 = _RAND_9[31:0];
  _RAND_10 = {1{`RANDOM}};
  regs_10 = _RAND_10[31:0];
  _RAND_11 = {1{`RANDOM}};
  regs_11 = _RAND_11[31:0];
  _RAND_12 = {1{`RANDOM}};
  regs_12 = _RAND_12[31:0];
  _RAND_13 = {1{`RANDOM}};
  regs_13 = _RAND_13[31:0];
  _RAND_14 = {1{`RANDOM}};
  regs_14 = _RAND_14[31:0];
  _RAND_15 = {1{`RANDOM}};
  regs_15 = _RAND_15[31:0];
  _RAND_16 = {1{`RANDOM}};
  regs_16 = _RAND_16[31:0];
  _RAND_17 = {1{`RANDOM}};
  regs_17 = _RAND_17[31:0];
  _RAND_18 = {1{`RANDOM}};
  regs_18 = _RAND_18[31:0];
  _RAND_19 = {1{`RANDOM}};
  regs_19 = _RAND_19[31:0];
  _RAND_20 = {1{`RANDOM}};
  regs_20 = _RAND_20[31:0];
  _RAND_21 = {1{`RANDOM}};
  regs_21 = _RAND_21[31:0];
  _RAND_22 = {1{`RANDOM}};
  regs_22 = _RAND_22[31:0];
  _RAND_23 = {1{`RANDOM}};
  regs_23 = _RAND_23[31:0];
  _RAND_24 = {1{`RANDOM}};
  regs_24 = _RAND_24[31:0];
  _RAND_25 = {1{`RANDOM}};
  regs_25 = _RAND_25[31:0];
  _RAND_26 = {1{`RANDOM}};
  regs_26 = _RAND_26[31:0];
  _RAND_27 = {1{`RANDOM}};
  regs_27 = _RAND_27[31:0];
  _RAND_28 = {1{`RANDOM}};
  regs_28 = _RAND_28[31:0];
  _RAND_29 = {1{`RANDOM}};
  regs_29 = _RAND_29[31:0];
  _RAND_30 = {1{`RANDOM}};
  regs_30 = _RAND_30[31:0];
  _RAND_31 = {1{`RANDOM}};
  regs_31 = _RAND_31[31:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module StageReg_1(
  input         clock,
  input         reset,
  input  [31:0] io_in_pc,
  input  [31:0] io_in_instruction,
  input  [31:0] io_in_sextImm,
  input  [31:0] io_in_readdata1,
  input  [31:0] io_in_readdata2,
  input         io_flush,
  output [31:0] io_data_pc,
  output [31:0] io_data_instruction,
  output [31:0] io_data_sextImm,
  output [31:0] io_data_readdata1,
  output [31:0] io_data_readdata2
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
`endif // RANDOMIZE_REG_INIT
  reg [31:0] reg_pc;
  reg [31:0] reg_instruction;
  reg [31:0] reg_sextImm;
  reg [31:0] reg_readdata1;
  reg [31:0] reg_readdata2;
  assign io_data_pc = reg_pc;
  assign io_data_instruction = reg_instruction;
  assign io_data_sextImm = reg_sextImm;
  assign io_data_readdata1 = reg_readdata1;
  assign io_data_readdata2 = reg_readdata2;
  always @(posedge clock) begin
    if (reset) begin
      reg_pc <= 32'h0;
    end else if (io_flush) begin
      reg_pc <= 32'h0;
    end else begin
      reg_pc <= io_in_pc;
    end
    if (reset) begin
      reg_instruction <= 32'h0;
    end else if (io_flush) begin
      reg_instruction <= 32'h0;
    end else begin
      reg_instruction <= io_in_instruction;
    end
    if (reset) begin
      reg_sextImm <= 32'h0;
    end else if (io_flush) begin
      reg_sextImm <= 32'h0;
    end else begin
      reg_sextImm <= io_in_sextImm;
    end
    if (reset) begin
      reg_readdata1 <= 32'h0;
    end else if (io_flush) begin
      reg_readdata1 <= 32'h0;
    end else begin
      reg_readdata1 <= io_in_readdata1;
    end
    if (reset) begin
      reg_readdata2 <= 32'h0;
    end else if (io_flush) begin
      reg_readdata2 <= 32'h0;
    end else begin
      reg_readdata2 <= io_in_readdata2;
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  reg_pc = _RAND_0[31:0];
  _RAND_1 = {1{`RANDOM}};
  reg_instruction = _RAND_1[31:0];
  _RAND_2 = {1{`RANDOM}};
  reg_sextImm = _RAND_2[31:0];
  _RAND_3 = {1{`RANDOM}};
  reg_readdata1 = _RAND_3[31:0];
  _RAND_4 = {1{`RANDOM}};
  reg_readdata2 = _RAND_4[31:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module StageReg_5(
  input         clock,
  input         reset,
  input  [31:0] io_in_instruction,
  input  [31:0] io_in_readdata,
  input  [31:0] io_in_ex_result,
  output [31:0] io_data_instruction,
  output [31:0] io_data_readdata,
  output [31:0] io_data_ex_result
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
`endif // RANDOMIZE_REG_INIT
  reg [31:0] reg_instruction;
  reg [31:0] reg_readdata;
  reg [31:0] reg_ex_result;
  assign io_data_instruction = reg_instruction;
  assign io_data_readdata = reg_readdata;
  assign io_data_ex_result = reg_ex_result;
  always @(posedge clock) begin
    if (reset) begin
      reg_instruction <= 32'h0;
    end else begin
      reg_instruction <= io_in_instruction;
    end
    if (reset) begin
      reg_readdata <= 32'h0;
    end else begin
      reg_readdata <= io_in_readdata;
    end
    if (reset) begin
      reg_ex_result <= 32'h0;
    end else begin
      reg_ex_result <= io_in_ex_result;
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  reg_instruction = _RAND_0[31:0];
  _RAND_1 = {1{`RANDOM}};
  reg_readdata = _RAND_1[31:0];
  _RAND_2 = {1{`RANDOM}};
  reg_ex_result = _RAND_2[31:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module StageReg_3(
  input         clock,
  input         reset,
  input  [31:0] io_in_ex_result,
  input  [31:0] io_in_mem_writedata,
  input  [31:0] io_in_instruction,
  input  [31:0] io_in_next_pc,
  input         io_in_taken,
  input         io_flush,
  output [31:0] io_data_ex_result,
  output [31:0] io_data_mem_writedata,
  output [31:0] io_data_instruction,
  output [31:0] io_data_next_pc,
  output        io_data_taken
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
`endif // RANDOMIZE_REG_INIT
  reg [31:0] reg_ex_result;
  reg [31:0] reg_mem_writedata;
  reg [31:0] reg_instruction;
  reg [31:0] reg_next_pc;
  reg  reg_taken;
  assign io_data_ex_result = reg_ex_result;
  assign io_data_mem_writedata = reg_mem_writedata;
  assign io_data_instruction = reg_instruction;
  assign io_data_next_pc = reg_next_pc;
  assign io_data_taken = reg_taken;
  always @(posedge clock) begin
    if (reset) begin
      reg_ex_result <= 32'h0;
    end else if (io_flush) begin
      reg_ex_result <= 32'h0;
    end else begin
      reg_ex_result <= io_in_ex_result;
    end
    if (reset) begin
      reg_mem_writedata <= 32'h0;
    end else if (io_flush) begin
      reg_mem_writedata <= 32'h0;
    end else begin
      reg_mem_writedata <= io_in_mem_writedata;
    end
    if (reset) begin
      reg_instruction <= 32'h0;
    end else if (io_flush) begin
      reg_instruction <= 32'h0;
    end else begin
      reg_instruction <= io_in_instruction;
    end
    if (reset) begin
      reg_next_pc <= 32'h0;
    end else if (io_flush) begin
      reg_next_pc <= 32'h0;
    end else begin
      reg_next_pc <= io_in_next_pc;
    end
    if (reset) begin
      reg_taken <= 1'h0;
    end else if (io_flush) begin
      reg_taken <= 1'h0;
    end else begin
      reg_taken <= io_in_taken;
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  reg_ex_result = _RAND_0[31:0];
  _RAND_1 = {1{`RANDOM}};
  reg_mem_writedata = _RAND_1[31:0];
  _RAND_2 = {1{`RANDOM}};
  reg_instruction = _RAND_2[31:0];
  _RAND_3 = {1{`RANDOM}};
  reg_next_pc = _RAND_3[31:0];
  _RAND_4 = {1{`RANDOM}};
  reg_taken = _RAND_4[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module StageReg_6(
  input   clock,
  input   reset,
  input   io_in_wb_ctrl_toreg,
  input   io_in_wb_ctrl_regwrite,
  output  io_data_wb_ctrl_toreg,
  output  io_data_wb_ctrl_regwrite
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
`endif // RANDOMIZE_REG_INIT
  reg  reg_wb_ctrl_toreg;
  reg  reg_wb_ctrl_regwrite;
  assign io_data_wb_ctrl_toreg = reg_wb_ctrl_toreg;
  assign io_data_wb_ctrl_regwrite = reg_wb_ctrl_regwrite;
  always @(posedge clock) begin
    if (reset) begin
      reg_wb_ctrl_toreg <= 1'h0;
    end else begin
      reg_wb_ctrl_toreg <= io_in_wb_ctrl_toreg;
    end
    if (reset) begin
      reg_wb_ctrl_regwrite <= 1'h0;
    end else begin
      reg_wb_ctrl_regwrite <= io_in_wb_ctrl_regwrite;
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  reg_wb_ctrl_toreg = _RAND_0[0:0];
  _RAND_1 = {1{`RANDOM}};
  reg_wb_ctrl_regwrite = _RAND_1[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module ALU(
  input  [3:0]  io_operation,
  input  [31:0] io_inputx,
  input  [31:0] io_inputy,
  output [31:0] io_result
);
  wire [31:0] _T_1 = io_inputx & io_inputy;
  wire [31:0] _T_3 = io_inputx | io_inputy;
  wire [31:0] _T_6 = io_inputx + io_inputy;
  wire [31:0] _T_9 = io_inputx - io_inputy;
  wire [31:0] _T_11 = io_inputx;
  wire [31:0] _T_14 = $signed(io_inputx) >>> io_inputy[4:0];
  wire [31:0] _T_18 = io_inputx ^ io_inputy;
  wire [31:0] _T_21 = io_inputx >> io_inputy[4:0];
  wire [31:0] _T_24 = io_inputy;
  wire [62:0] _GEN_15 = {{31'd0}, io_inputx};
  wire [62:0] _T_28 = _GEN_15 << io_inputy[4:0];
  wire [31:0] _T_31 = ~_T_3;
  wire  _GEN_1 = io_operation == 4'hd ? io_inputx == io_inputy : io_operation == 4'he & io_inputx != io_inputy; // @[]
  wire  _GEN_2 = io_operation == 4'hc ? io_inputx >= io_inputy : _GEN_1; // @[]
  wire  _GEN_3 = io_operation == 4'hb ? $signed(io_inputx) >= $signed(io_inputy) : _GEN_2; // @[]
  wire [31:0] _GEN_4 = io_operation == 4'ha ? _T_31 : {{31'd0}, _GEN_3}; // @[]
  wire [62:0] _GEN_5 = io_operation == 4'h8 ? _T_28 : {{31'd0}, _GEN_4}; // @[]
  wire [62:0] _GEN_6 = io_operation == 4'h9 ? {{62'd0}, $signed(_T_11) < $signed(_T_24)} : _GEN_5; // @[]
  wire [62:0] _GEN_7 = io_operation == 4'h2 ? {{31'd0}, _T_21} : _GEN_6; // @[]
  wire [62:0] _GEN_8 = io_operation == 4'h0 ? {{31'd0}, _T_18} : _GEN_7; // @[]
  wire [62:0] _GEN_9 = io_operation == 4'h1 ? {{62'd0}, io_inputx < io_inputy} : _GEN_8; // @[]
  wire [62:0] _GEN_10 = io_operation == 4'h3 ? {{31'd0}, _T_14} : _GEN_9; // @[]
  wire [62:0] _GEN_11 = io_operation == 4'h4 ? {{31'd0}, _T_9} : _GEN_10; // @[]
  wire [62:0] _GEN_12 = io_operation == 4'h7 ? {{31'd0}, _T_6} : _GEN_11; // @[]
  wire [62:0] _GEN_13 = io_operation == 4'h5 ? {{31'd0}, _T_3} : _GEN_12; // @[]
  wire [62:0] _GEN_14 = io_operation == 4'h6 ? {{31'd0}, _T_1} : _GEN_13; // @[]
  assign io_result = _GEN_14[31:0];
endmodule
module ImmediateGenerator(
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
module HazardUnitBP(
  input  [4:0] io_rs1,
  input  [4:0] io_rs2,
  input        io_idex_memread,
  input  [4:0] io_idex_rd,
  input        io_exmem_taken,
  output [1:0] io_pcSel,
  output       io_if_id_stall,
  output       io_if_id_flush,
  output       io_id_ex_flush,
  output       io_ex_mem_flush
);
  wire [1:0] _GEN_3 = io_idex_memread & (io_idex_rd == io_rs1 | io_idex_rd == io_rs2) ? 2'h3 : 2'h0; // @[]
  assign io_pcSel = io_exmem_taken ? 2'h1 : _GEN_3; // @[]
  assign io_if_id_stall = io_exmem_taken ? 1'h0 : io_idex_memread & (io_idex_rd == io_rs1 | io_idex_rd == io_rs2); // @[]
  assign io_if_id_flush = io_exmem_taken; // @[]
  assign io_id_ex_flush = io_exmem_taken | io_idex_memread & (io_idex_rd == io_rs1 | io_idex_rd == io_rs2); // @[]
  assign io_ex_mem_flush = io_exmem_taken; // @[]
endmodule
module StageReg(
  input         clock,
  input         reset,
  input  [31:0] io_in_instruction,
  input  [31:0] io_in_pc,
  input         io_flush,
  input         io_valid,
  output [31:0] io_data_instruction,
  output [31:0] io_data_pc
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
`endif // RANDOMIZE_REG_INIT
  reg [31:0] reg_instruction;
  reg [31:0] reg_pc;
  assign io_data_instruction = reg_instruction;
  assign io_data_pc = reg_pc;
  always @(posedge clock) begin
    if (reset) begin
      reg_instruction <= 32'h0;
    end else if (io_flush) begin
      reg_instruction <= 32'h0;
    end else if (io_valid) begin
      reg_instruction <= io_in_instruction;
    end
    if (reset) begin
      reg_pc <= 32'h0;
    end else if (io_flush) begin
      reg_pc <= 32'h0;
    end else if (io_valid) begin
      reg_pc <= io_in_pc;
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  reg_instruction = _RAND_0[31:0];
  _RAND_1 = {1{`RANDOM}};
  reg_pc = _RAND_1[31:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module ALUControl(
  input        io_aluop,
  input        io_itype,
  input  [6:0] io_funct7,
  input  [2:0] io_funct3,
  output [3:0] io_operation
);
  wire [2:0] _GEN_0 = io_itype | io_funct7 == 7'h0 ? 3'h7 : 3'h4; // @[]
  wire [1:0] _GEN_1 = io_funct7 == 7'h0 ? 2'h2 : 2'h3; // @[]
  wire [2:0] _GEN_2 = io_funct3 == 3'h6 ? 3'h5 : 3'h6; // @[]
  wire [2:0] _GEN_3 = io_funct3 == 3'h5 ? {{1'd0}, _GEN_1} : _GEN_2; // @[]
  wire [2:0] _GEN_4 = io_funct3 == 3'h4 ? 3'h0 : _GEN_3; // @[]
  wire [2:0] _GEN_5 = io_funct3 == 3'h3 ? 3'h1 : _GEN_4; // @[]
  wire [3:0] _GEN_6 = io_funct3 == 3'h2 ? 4'h9 : {{1'd0}, _GEN_5}; // @[]
  wire [3:0] _GEN_7 = io_funct3 == 3'h1 ? 4'h8 : _GEN_6; // @[]
  wire [3:0] _GEN_8 = io_funct3 == 3'h0 ? {{1'd0}, _GEN_0} : _GEN_7; // @[]
  assign io_operation = io_aluop ? _GEN_8 : 4'h7; // @[]
endmodule
module PipelinedCPUBP(
  input         clock,
  input         reset,
  output [31:0] io_imem_address,
  output        io_imem_valid,
  input         io_imem_good,
  input  [31:0] io_imem_instruction,
  input         io_imem_ready,
  output [31:0] io_dmem_address,
  output        io_dmem_valid,
  input         io_dmem_good,
  output [31:0] io_dmem_writedata,
  output        io_dmem_memread,
  output        io_dmem_memwrite,
  output [1:0]  io_dmem_maskmode,
  output        io_dmem_sext,
  input  [31:0] io_dmem_readdata
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
`endif // RANDOMIZE_REG_INIT
  wire [6:0] control_io_opcode;
  wire  control_io_itype;
  wire  control_io_aluop;
  wire  control_io_xsrc;
  wire  control_io_ysrc;
  wire  control_io_branch;
  wire  control_io_jal;
  wire  control_io_jalr;
  wire  control_io_plus4;
  wire  control_io_resultselect;
  wire [1:0] control_io_memop;
  wire  control_io_toreg;
  wire  control_io_regwrite;
  wire  registers_clock;
  wire [4:0] registers_io_readreg1;
  wire [4:0] registers_io_readreg2;
  wire [4:0] registers_io_writereg;
  wire [31:0] registers_io_writedata;
  wire  registers_io_wen;
  wire [31:0] registers_io_readdata1;
  wire [31:0] registers_io_readdata2;
  wire  aluControl_io_aluop;
  wire  aluControl_io_itype;
  wire [6:0] aluControl_io_funct7;
  wire [2:0] aluControl_io_funct3;
  wire [3:0] aluControl_io_operation;
  wire [3:0] alu_io_operation;
  wire [31:0] alu_io_inputx;
  wire [31:0] alu_io_inputy;
  wire [31:0] alu_io_result;
  wire [31:0] immGen_io_instruction;
  wire [31:0] immGen_io_sextImm;
  wire [31:0] pcPlusFour_io_inputx;
  wire [31:0] pcPlusFour_io_inputy;
  wire [31:0] pcPlusFour_io_result;
  wire  nextPCmod_io_branch;
  wire  nextPCmod_io_jal;
  wire  nextPCmod_io_jalr;
  wire [31:0] nextPCmod_io_inputx;
  wire [31:0] nextPCmod_io_inputy;
  wire [2:0] nextPCmod_io_funct3;
  wire [31:0] nextPCmod_io_pc;
  wire [31:0] nextPCmod_io_imm;
  wire [31:0] nextPCmod_io_nextpc;
  wire  nextPCmod_io_taken;
  wire [4:0] forwarding_io_rs1;
  wire [4:0] forwarding_io_rs2;
  wire [4:0] forwarding_io_exmemrd;
  wire  forwarding_io_exmemrw;
  wire [4:0] forwarding_io_memwbrd;
  wire  forwarding_io_memwbrw;
  wire [1:0] forwarding_io_forwardA;
  wire [1:0] forwarding_io_forwardB;
  wire [4:0] hazard_io_rs1;
  wire [4:0] hazard_io_rs2;
  wire  hazard_io_idex_memread;
  wire [4:0] hazard_io_idex_rd;
  wire  hazard_io_exmem_taken;
  wire [1:0] hazard_io_pcSel;
  wire  hazard_io_if_id_stall;
  wire  hazard_io_if_id_flush;
  wire  hazard_io_id_ex_flush;
  wire  hazard_io_ex_mem_flush;
  wire [31:0] branchAdd_io_inputx;
  wire [31:0] branchAdd_io_inputy;
  wire [31:0] branchAdd_io_result;
  wire  if_id_clock;
  wire  if_id_reset;
  wire [31:0] if_id_io_in_instruction;
  wire [31:0] if_id_io_in_pc;
  wire  if_id_io_flush;
  wire  if_id_io_valid;
  wire [31:0] if_id_io_data_instruction;
  wire [31:0] if_id_io_data_pc;
  wire  id_ex_clock;
  wire  id_ex_reset;
  wire [31:0] id_ex_io_in_pc;
  wire [31:0] id_ex_io_in_instruction;
  wire [31:0] id_ex_io_in_sextImm;
  wire [31:0] id_ex_io_in_readdata1;
  wire [31:0] id_ex_io_in_readdata2;
  wire  id_ex_io_flush;
  wire [31:0] id_ex_io_data_pc;
  wire [31:0] id_ex_io_data_instruction;
  wire [31:0] id_ex_io_data_sextImm;
  wire [31:0] id_ex_io_data_readdata1;
  wire [31:0] id_ex_io_data_readdata2;
  wire  id_ex_ctrl_clock;
  wire  id_ex_ctrl_reset;
  wire  id_ex_ctrl_io_in_ex_ctrl_itype;
  wire  id_ex_ctrl_io_in_ex_ctrl_aluop;
  wire  id_ex_ctrl_io_in_ex_ctrl_resultselect;
  wire  id_ex_ctrl_io_in_ex_ctrl_xsrc;
  wire  id_ex_ctrl_io_in_ex_ctrl_ysrc;
  wire  id_ex_ctrl_io_in_ex_ctrl_plus4;
  wire  id_ex_ctrl_io_in_ex_ctrl_branch;
  wire  id_ex_ctrl_io_in_ex_ctrl_jal;
  wire  id_ex_ctrl_io_in_ex_ctrl_jalr;
  wire [1:0] id_ex_ctrl_io_in_mem_ctrl_memop;
  wire  id_ex_ctrl_io_in_wb_ctrl_toreg;
  wire  id_ex_ctrl_io_in_wb_ctrl_regwrite;
  wire  id_ex_ctrl_io_flush;
  wire  id_ex_ctrl_io_data_ex_ctrl_itype;
  wire  id_ex_ctrl_io_data_ex_ctrl_aluop;
  wire  id_ex_ctrl_io_data_ex_ctrl_resultselect;
  wire  id_ex_ctrl_io_data_ex_ctrl_xsrc;
  wire  id_ex_ctrl_io_data_ex_ctrl_ysrc;
  wire  id_ex_ctrl_io_data_ex_ctrl_plus4;
  wire  id_ex_ctrl_io_data_ex_ctrl_branch;
  wire  id_ex_ctrl_io_data_ex_ctrl_jal;
  wire  id_ex_ctrl_io_data_ex_ctrl_jalr;
  wire [1:0] id_ex_ctrl_io_data_mem_ctrl_memop;
  wire  id_ex_ctrl_io_data_wb_ctrl_toreg;
  wire  id_ex_ctrl_io_data_wb_ctrl_regwrite;
  wire  ex_mem_clock;
  wire  ex_mem_reset;
  wire [31:0] ex_mem_io_in_ex_result;
  wire [31:0] ex_mem_io_in_mem_writedata;
  wire [31:0] ex_mem_io_in_instruction;
  wire [31:0] ex_mem_io_in_next_pc;
  wire  ex_mem_io_in_taken;
  wire  ex_mem_io_flush;
  wire [31:0] ex_mem_io_data_ex_result;
  wire [31:0] ex_mem_io_data_mem_writedata;
  wire [31:0] ex_mem_io_data_instruction;
  wire [31:0] ex_mem_io_data_next_pc;
  wire  ex_mem_io_data_taken;
  wire  ex_mem_ctrl_clock;
  wire  ex_mem_ctrl_reset;
  wire [1:0] ex_mem_ctrl_io_in_mem_ctrl_memop;
  wire  ex_mem_ctrl_io_in_wb_ctrl_toreg;
  wire  ex_mem_ctrl_io_in_wb_ctrl_regwrite;
  wire  ex_mem_ctrl_io_flush;
  wire [1:0] ex_mem_ctrl_io_data_mem_ctrl_memop;
  wire  ex_mem_ctrl_io_data_wb_ctrl_toreg;
  wire  ex_mem_ctrl_io_data_wb_ctrl_regwrite;
  wire  mem_wb_clock;
  wire  mem_wb_reset;
  wire [31:0] mem_wb_io_in_instruction;
  wire [31:0] mem_wb_io_in_readdata;
  wire [31:0] mem_wb_io_in_ex_result;
  wire [31:0] mem_wb_io_data_instruction;
  wire [31:0] mem_wb_io_data_readdata;
  wire [31:0] mem_wb_io_data_ex_result;
  wire  mem_wb_ctrl_clock;
  wire  mem_wb_ctrl_reset;
  wire  mem_wb_ctrl_io_in_wb_ctrl_toreg;
  wire  mem_wb_ctrl_io_in_wb_ctrl_regwrite;
  wire  mem_wb_ctrl_io_data_wb_ctrl_toreg;
  wire  mem_wb_ctrl_io_data_wb_ctrl_regwrite;
  reg [31:0] pc;
  reg [31:0] bpCorrect;
  reg [31:0] bpIncorrect;
  wire [31:0] _T_12 = hazard_io_pcSel == 2'h3 ? pc : 32'h0;
  wire [31:0] id_next_pc = branchAdd_io_result;
  wire [31:0] next_pc = ex_mem_io_data_next_pc;
  wire [31:0] write_data = mem_wb_ctrl_io_data_wb_ctrl_toreg ? mem_wb_io_data_readdata : mem_wb_io_data_ex_result; // @[]
  wire [31:0] _GEN_3 = forwarding_io_forwardA == 2'h1 ? ex_mem_io_data_ex_result : write_data; // @[]
  wire [31:0] forward_a_mux = forwarding_io_forwardA == 2'h0 ? id_ex_io_data_readdata1 : _GEN_3; // @[]
  wire [31:0] _GEN_5 = forwarding_io_forwardB == 2'h1 ? ex_mem_io_data_ex_result : write_data; // @[]
  wire [31:0] forward_b_mux = forwarding_io_forwardB == 2'h0 ? id_ex_io_data_readdata2 : _GEN_5; // @[]
  wire [31:0] _GEN_8 = id_ex_ctrl_io_data_ex_ctrl_ysrc ? id_ex_io_data_sextImm : forward_b_mux; // @[]
  wire [31:0] _T_39 = bpCorrect + 32'h1;
  wire [31:0] _T_41 = bpIncorrect + 32'h1;
  Control control (
    .io_opcode(control_io_opcode),
    .io_itype(control_io_itype),
    .io_aluop(control_io_aluop),
    .io_xsrc(control_io_xsrc),
    .io_ysrc(control_io_ysrc),
    .io_branch(control_io_branch),
    .io_jal(control_io_jal),
    .io_jalr(control_io_jalr),
    .io_plus4(control_io_plus4),
    .io_resultselect(control_io_resultselect),
    .io_memop(control_io_memop),
    .io_toreg(control_io_toreg),
    .io_regwrite(control_io_regwrite)
  );
  RegisterFile registers (
    .clock(registers_clock),
    .io_readreg1(registers_io_readreg1),
    .io_readreg2(registers_io_readreg2),
    .io_writereg(registers_io_writereg),
    .io_writedata(registers_io_writedata),
    .io_wen(registers_io_wen),
    .io_readdata1(registers_io_readdata1),
    .io_readdata2(registers_io_readdata2)
  );
  ALUControl aluControl (
    .io_aluop(aluControl_io_aluop),
    .io_itype(aluControl_io_itype),
    .io_funct7(aluControl_io_funct7),
    .io_funct3(aluControl_io_funct3),
    .io_operation(aluControl_io_operation)
  );
  ALU alu (
    .io_operation(alu_io_operation),
    .io_inputx(alu_io_inputx),
    .io_inputy(alu_io_inputy),
    .io_result(alu_io_result)
  );
  ImmediateGenerator immGen (
    .io_instruction(immGen_io_instruction),
    .io_sextImm(immGen_io_sextImm)
  );
  Adder pcPlusFour (
    .io_inputx(pcPlusFour_io_inputx),
    .io_inputy(pcPlusFour_io_inputy),
    .io_result(pcPlusFour_io_result)
  );
  NextPC nextPCmod (
    .io_branch(nextPCmod_io_branch),
    .io_jal(nextPCmod_io_jal),
    .io_jalr(nextPCmod_io_jalr),
    .io_inputx(nextPCmod_io_inputx),
    .io_inputy(nextPCmod_io_inputy),
    .io_funct3(nextPCmod_io_funct3),
    .io_pc(nextPCmod_io_pc),
    .io_imm(nextPCmod_io_imm),
    .io_nextpc(nextPCmod_io_nextpc),
    .io_taken(nextPCmod_io_taken)
  );
  ForwardingUnit forwarding (
    .io_rs1(forwarding_io_rs1),
    .io_rs2(forwarding_io_rs2),
    .io_exmemrd(forwarding_io_exmemrd),
    .io_exmemrw(forwarding_io_exmemrw),
    .io_memwbrd(forwarding_io_memwbrd),
    .io_memwbrw(forwarding_io_memwbrw),
    .io_forwardA(forwarding_io_forwardA),
    .io_forwardB(forwarding_io_forwardB)
  );
  HazardUnitBP hazard (
    .io_rs1(hazard_io_rs1),
    .io_rs2(hazard_io_rs2),
    .io_idex_memread(hazard_io_idex_memread),
    .io_idex_rd(hazard_io_idex_rd),
    .io_exmem_taken(hazard_io_exmem_taken),
    .io_pcSel(hazard_io_pcSel),
    .io_if_id_stall(hazard_io_if_id_stall),
    .io_if_id_flush(hazard_io_if_id_flush),
    .io_id_ex_flush(hazard_io_id_ex_flush),
    .io_ex_mem_flush(hazard_io_ex_mem_flush)
  );
  Adder branchAdd (
    .io_inputx(branchAdd_io_inputx),
    .io_inputy(branchAdd_io_inputy),
    .io_result(branchAdd_io_result)
  );
  StageReg if_id (
    .clock(if_id_clock),
    .reset(if_id_reset),
    .io_in_instruction(if_id_io_in_instruction),
    .io_in_pc(if_id_io_in_pc),
    .io_flush(if_id_io_flush),
    .io_valid(if_id_io_valid),
    .io_data_instruction(if_id_io_data_instruction),
    .io_data_pc(if_id_io_data_pc)
  );
  StageReg_1 id_ex (
    .clock(id_ex_clock),
    .reset(id_ex_reset),
    .io_in_pc(id_ex_io_in_pc),
    .io_in_instruction(id_ex_io_in_instruction),
    .io_in_sextImm(id_ex_io_in_sextImm),
    .io_in_readdata1(id_ex_io_in_readdata1),
    .io_in_readdata2(id_ex_io_in_readdata2),
    .io_flush(id_ex_io_flush),
    .io_data_pc(id_ex_io_data_pc),
    .io_data_instruction(id_ex_io_data_instruction),
    .io_data_sextImm(id_ex_io_data_sextImm),
    .io_data_readdata1(id_ex_io_data_readdata1),
    .io_data_readdata2(id_ex_io_data_readdata2)
  );
  StageReg_2 id_ex_ctrl (
    .clock(id_ex_ctrl_clock),
    .reset(id_ex_ctrl_reset),
    .io_in_ex_ctrl_itype(id_ex_ctrl_io_in_ex_ctrl_itype),
    .io_in_ex_ctrl_aluop(id_ex_ctrl_io_in_ex_ctrl_aluop),
    .io_in_ex_ctrl_resultselect(id_ex_ctrl_io_in_ex_ctrl_resultselect),
    .io_in_ex_ctrl_xsrc(id_ex_ctrl_io_in_ex_ctrl_xsrc),
    .io_in_ex_ctrl_ysrc(id_ex_ctrl_io_in_ex_ctrl_ysrc),
    .io_in_ex_ctrl_plus4(id_ex_ctrl_io_in_ex_ctrl_plus4),
    .io_in_ex_ctrl_branch(id_ex_ctrl_io_in_ex_ctrl_branch),
    .io_in_ex_ctrl_jal(id_ex_ctrl_io_in_ex_ctrl_jal),
    .io_in_ex_ctrl_jalr(id_ex_ctrl_io_in_ex_ctrl_jalr),
    .io_in_mem_ctrl_memop(id_ex_ctrl_io_in_mem_ctrl_memop),
    .io_in_wb_ctrl_toreg(id_ex_ctrl_io_in_wb_ctrl_toreg),
    .io_in_wb_ctrl_regwrite(id_ex_ctrl_io_in_wb_ctrl_regwrite),
    .io_flush(id_ex_ctrl_io_flush),
    .io_data_ex_ctrl_itype(id_ex_ctrl_io_data_ex_ctrl_itype),
    .io_data_ex_ctrl_aluop(id_ex_ctrl_io_data_ex_ctrl_aluop),
    .io_data_ex_ctrl_resultselect(id_ex_ctrl_io_data_ex_ctrl_resultselect),
    .io_data_ex_ctrl_xsrc(id_ex_ctrl_io_data_ex_ctrl_xsrc),
    .io_data_ex_ctrl_ysrc(id_ex_ctrl_io_data_ex_ctrl_ysrc),
    .io_data_ex_ctrl_plus4(id_ex_ctrl_io_data_ex_ctrl_plus4),
    .io_data_ex_ctrl_branch(id_ex_ctrl_io_data_ex_ctrl_branch),
    .io_data_ex_ctrl_jal(id_ex_ctrl_io_data_ex_ctrl_jal),
    .io_data_ex_ctrl_jalr(id_ex_ctrl_io_data_ex_ctrl_jalr),
    .io_data_mem_ctrl_memop(id_ex_ctrl_io_data_mem_ctrl_memop),
    .io_data_wb_ctrl_toreg(id_ex_ctrl_io_data_wb_ctrl_toreg),
    .io_data_wb_ctrl_regwrite(id_ex_ctrl_io_data_wb_ctrl_regwrite)
  );
  StageReg_3 ex_mem (
    .clock(ex_mem_clock),
    .reset(ex_mem_reset),
    .io_in_ex_result(ex_mem_io_in_ex_result),
    .io_in_mem_writedata(ex_mem_io_in_mem_writedata),
    .io_in_instruction(ex_mem_io_in_instruction),
    .io_in_next_pc(ex_mem_io_in_next_pc),
    .io_in_taken(ex_mem_io_in_taken),
    .io_flush(ex_mem_io_flush),
    .io_data_ex_result(ex_mem_io_data_ex_result),
    .io_data_mem_writedata(ex_mem_io_data_mem_writedata),
    .io_data_instruction(ex_mem_io_data_instruction),
    .io_data_next_pc(ex_mem_io_data_next_pc),
    .io_data_taken(ex_mem_io_data_taken)
  );
  StageReg_4 ex_mem_ctrl (
    .clock(ex_mem_ctrl_clock),
    .reset(ex_mem_ctrl_reset),
    .io_in_mem_ctrl_memop(ex_mem_ctrl_io_in_mem_ctrl_memop),
    .io_in_wb_ctrl_toreg(ex_mem_ctrl_io_in_wb_ctrl_toreg),
    .io_in_wb_ctrl_regwrite(ex_mem_ctrl_io_in_wb_ctrl_regwrite),
    .io_flush(ex_mem_ctrl_io_flush),
    .io_data_mem_ctrl_memop(ex_mem_ctrl_io_data_mem_ctrl_memop),
    .io_data_wb_ctrl_toreg(ex_mem_ctrl_io_data_wb_ctrl_toreg),
    .io_data_wb_ctrl_regwrite(ex_mem_ctrl_io_data_wb_ctrl_regwrite)
  );
  StageReg_5 mem_wb (
    .clock(mem_wb_clock),
    .reset(mem_wb_reset),
    .io_in_instruction(mem_wb_io_in_instruction),
    .io_in_readdata(mem_wb_io_in_readdata),
    .io_in_ex_result(mem_wb_io_in_ex_result),
    .io_data_instruction(mem_wb_io_data_instruction),
    .io_data_readdata(mem_wb_io_data_readdata),
    .io_data_ex_result(mem_wb_io_data_ex_result)
  );
  StageReg_6 mem_wb_ctrl (
    .clock(mem_wb_ctrl_clock),
    .reset(mem_wb_ctrl_reset),
    .io_in_wb_ctrl_toreg(mem_wb_ctrl_io_in_wb_ctrl_toreg),
    .io_in_wb_ctrl_regwrite(mem_wb_ctrl_io_in_wb_ctrl_regwrite),
    .io_data_wb_ctrl_toreg(mem_wb_ctrl_io_data_wb_ctrl_toreg),
    .io_data_wb_ctrl_regwrite(mem_wb_ctrl_io_data_wb_ctrl_regwrite)
  );
  assign io_imem_address = pc;
  assign io_imem_valid = 1'h1;
  assign io_dmem_address = ex_mem_io_data_ex_result;
  assign io_dmem_valid = ex_mem_ctrl_io_data_mem_ctrl_memop[1];
  assign io_dmem_writedata = ex_mem_io_data_mem_writedata;
  assign io_dmem_memread = ex_mem_ctrl_io_data_mem_ctrl_memop == 2'h2;
  assign io_dmem_memwrite = ex_mem_ctrl_io_data_mem_ctrl_memop == 2'h3;
  assign io_dmem_maskmode = ex_mem_io_data_instruction[13:12];
  assign io_dmem_sext = ~ex_mem_io_data_instruction[14];
  assign control_io_opcode = if_id_io_data_instruction[6:0];
  assign registers_clock = clock;
  assign registers_io_readreg1 = if_id_io_data_instruction[19:15];
  assign registers_io_readreg2 = if_id_io_data_instruction[24:20];
  assign registers_io_writereg = mem_wb_io_data_instruction[11:7];
  assign registers_io_writedata = mem_wb_ctrl_io_data_wb_ctrl_toreg ? mem_wb_io_data_readdata : mem_wb_io_data_ex_result
    ; // @[]
  assign registers_io_wen = mem_wb_io_data_instruction[11:7] == 5'h0 ? 1'h0 : mem_wb_ctrl_io_data_wb_ctrl_regwrite; // @[]
  assign aluControl_io_aluop = id_ex_ctrl_io_data_ex_ctrl_aluop;
  assign aluControl_io_itype = id_ex_ctrl_io_data_ex_ctrl_itype;
  assign aluControl_io_funct7 = id_ex_io_data_instruction[31:25];
  assign aluControl_io_funct3 = id_ex_io_data_instruction[14:12];
  assign alu_io_operation = aluControl_io_operation;
  assign alu_io_inputx = id_ex_ctrl_io_data_ex_ctrl_xsrc ? id_ex_io_data_pc : forward_a_mux; // @[]
  assign alu_io_inputy = id_ex_ctrl_io_data_ex_ctrl_plus4 ? 32'h4 : _GEN_8; // @[]
  assign immGen_io_instruction = if_id_io_data_instruction;
  assign pcPlusFour_io_inputx = pc;
  assign pcPlusFour_io_inputy = 32'h4;
  assign nextPCmod_io_branch = id_ex_ctrl_io_data_ex_ctrl_branch;
  assign nextPCmod_io_jal = id_ex_ctrl_io_data_ex_ctrl_jal;
  assign nextPCmod_io_jalr = id_ex_ctrl_io_data_ex_ctrl_jalr;
  assign nextPCmod_io_inputx = forwarding_io_forwardA == 2'h0 ? id_ex_io_data_readdata1 : _GEN_3; // @[]
  assign nextPCmod_io_inputy = forwarding_io_forwardB == 2'h0 ? id_ex_io_data_readdata2 : _GEN_5; // @[]
  assign nextPCmod_io_funct3 = id_ex_io_data_instruction[14:12];
  assign nextPCmod_io_pc = id_ex_io_data_pc;
  assign nextPCmod_io_imm = id_ex_io_data_sextImm;
  assign forwarding_io_rs1 = id_ex_io_data_instruction[19:15];
  assign forwarding_io_rs2 = id_ex_io_data_instruction[24:20];
  assign forwarding_io_exmemrd = ex_mem_io_data_instruction[11:7];
  assign forwarding_io_exmemrw = ex_mem_ctrl_io_data_wb_ctrl_regwrite;
  assign forwarding_io_memwbrd = mem_wb_io_data_instruction[11:7];
  assign forwarding_io_memwbrw = mem_wb_ctrl_io_data_wb_ctrl_regwrite;
  assign hazard_io_rs1 = if_id_io_data_instruction[19:15];
  assign hazard_io_rs2 = if_id_io_data_instruction[24:20];
  assign hazard_io_idex_memread = id_ex_ctrl_io_data_mem_ctrl_memop == 2'h2; // @[]
  assign hazard_io_idex_rd = id_ex_io_data_instruction[11:7];
  assign hazard_io_exmem_taken = ex_mem_io_data_taken;
  assign branchAdd_io_inputx = if_id_io_data_pc;
  assign branchAdd_io_inputy = immGen_io_sextImm;
  assign if_id_clock = clock;
  assign if_id_reset = reset;
  assign if_id_io_in_instruction = io_imem_instruction;
  assign if_id_io_in_pc = pc;
  assign if_id_io_flush = hazard_io_if_id_flush;
  assign if_id_io_valid = ~hazard_io_if_id_stall;
  assign id_ex_clock = clock;
  assign id_ex_reset = reset;
  assign id_ex_io_in_pc = if_id_io_data_pc;
  assign id_ex_io_in_instruction = if_id_io_data_instruction;
  assign id_ex_io_in_sextImm = immGen_io_sextImm;
  assign id_ex_io_in_readdata1 = registers_io_readdata1;
  assign id_ex_io_in_readdata2 = registers_io_readdata2;
  assign id_ex_io_flush = hazard_io_id_ex_flush;
  assign id_ex_ctrl_clock = clock;
  assign id_ex_ctrl_reset = reset;
  assign id_ex_ctrl_io_in_ex_ctrl_itype = control_io_itype;
  assign id_ex_ctrl_io_in_ex_ctrl_aluop = control_io_aluop;
  assign id_ex_ctrl_io_in_ex_ctrl_resultselect = control_io_resultselect;
  assign id_ex_ctrl_io_in_ex_ctrl_xsrc = control_io_xsrc;
  assign id_ex_ctrl_io_in_ex_ctrl_ysrc = control_io_ysrc;
  assign id_ex_ctrl_io_in_ex_ctrl_plus4 = control_io_plus4;
  assign id_ex_ctrl_io_in_ex_ctrl_branch = control_io_branch;
  assign id_ex_ctrl_io_in_ex_ctrl_jal = control_io_jal;
  assign id_ex_ctrl_io_in_ex_ctrl_jalr = control_io_jalr;
  assign id_ex_ctrl_io_in_mem_ctrl_memop = control_io_memop;
  assign id_ex_ctrl_io_in_wb_ctrl_toreg = control_io_toreg;
  assign id_ex_ctrl_io_in_wb_ctrl_regwrite = control_io_regwrite;
  assign id_ex_ctrl_io_flush = hazard_io_id_ex_flush;
  assign ex_mem_clock = clock;
  assign ex_mem_reset = reset;
  assign ex_mem_io_in_ex_result = ~id_ex_ctrl_io_data_ex_ctrl_resultselect ? alu_io_result : id_ex_io_data_sextImm; // @[]
  assign ex_mem_io_in_mem_writedata = forwarding_io_forwardB == 2'h0 ? id_ex_io_data_readdata2 : _GEN_5; // @[]
  assign ex_mem_io_in_instruction = id_ex_io_data_instruction;
  assign ex_mem_io_in_next_pc = nextPCmod_io_nextpc;
  assign ex_mem_io_in_taken = id_ex_ctrl_io_data_ex_ctrl_branch ? nextPCmod_io_taken : nextPCmod_io_taken; // @[]
  assign ex_mem_io_flush = hazard_io_ex_mem_flush;
  assign ex_mem_ctrl_clock = clock;
  assign ex_mem_ctrl_reset = reset;
  assign ex_mem_ctrl_io_in_mem_ctrl_memop = id_ex_ctrl_io_data_mem_ctrl_memop;
  assign ex_mem_ctrl_io_in_wb_ctrl_toreg = id_ex_ctrl_io_data_wb_ctrl_toreg;
  assign ex_mem_ctrl_io_in_wb_ctrl_regwrite = id_ex_ctrl_io_data_wb_ctrl_regwrite;
  assign ex_mem_ctrl_io_flush = hazard_io_ex_mem_flush;
  assign mem_wb_clock = clock;
  assign mem_wb_reset = reset;
  assign mem_wb_io_in_instruction = ex_mem_io_data_instruction;
  assign mem_wb_io_in_readdata = io_dmem_readdata;
  assign mem_wb_io_in_ex_result = ex_mem_io_data_ex_result;
  assign mem_wb_ctrl_clock = clock;
  assign mem_wb_ctrl_reset = reset;
  assign mem_wb_ctrl_io_in_wb_ctrl_toreg = ex_mem_ctrl_io_data_wb_ctrl_toreg;
  assign mem_wb_ctrl_io_in_wb_ctrl_regwrite = ex_mem_ctrl_io_data_wb_ctrl_regwrite;
  always @(posedge clock) begin
    if (reset) begin
      pc <= 32'h0;
    end else if (hazard_io_pcSel == 2'h0) begin
      pc <= pcPlusFour_io_result;
    end else if (hazard_io_pcSel == 2'h1) begin
      pc <= next_pc;
    end else if (hazard_io_pcSel == 2'h2) begin
      pc <= id_next_pc;
    end else begin
      pc <= _T_12;
    end
    if (reset) begin
      bpCorrect <= 32'h0;
    end else if (id_ex_ctrl_io_data_ex_ctrl_branch & ~hazard_io_ex_mem_flush) begin
      if (~nextPCmod_io_taken) begin
        bpCorrect <= _T_39;
      end
    end
    if (reset) begin
      bpIncorrect <= 32'h0;
    end else if (id_ex_ctrl_io_data_ex_ctrl_branch & ~hazard_io_ex_mem_flush) begin
      if (!(~nextPCmod_io_taken)) begin
        bpIncorrect <= _T_41;
      end
    end
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (bpCorrect > 32'h100000 & ~reset) begin
          $fwrite(32'h80000002,"BP correct: %d; incorrect: %d\n",bpCorrect,bpIncorrect);
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  pc = _RAND_0[31:0];
  _RAND_1 = {1{`RANDOM}};
  bpCorrect = _RAND_1[31:0];
  _RAND_2 = {1{`RANDOM}};
  bpIncorrect = _RAND_2[31:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
