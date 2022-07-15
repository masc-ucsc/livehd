module RegisterFile(
  input         clock,
  input         reset,
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
