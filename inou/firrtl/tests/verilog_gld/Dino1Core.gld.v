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
  wire  _T_1 = 7'h33 == io_opcode; // @[Lookup.scala 31:38]
  wire  _T_3 = 7'h13 == io_opcode; // @[Lookup.scala 31:38]
  wire  _T_5 = 7'h3 == io_opcode; // @[Lookup.scala 31:38]
  wire  _T_7 = 7'h23 == io_opcode; // @[Lookup.scala 31:38]
  wire  _T_9 = 7'h63 == io_opcode; // @[Lookup.scala 31:38]
  wire  _T_11 = 7'h37 == io_opcode; // @[Lookup.scala 31:38]
  wire  _T_13 = 7'h17 == io_opcode; // @[Lookup.scala 31:38]
  wire  _T_15 = 7'h6f == io_opcode; // @[Lookup.scala 31:38]
  wire  _T_17 = 7'h67 == io_opcode; // @[Lookup.scala 31:38]
  wire  _T_37 = _T_11 ? 1'h0 : _T_13 | (_T_15 | _T_17); // @[Lookup.scala 33:37]
  wire  _T_38 = _T_9 ? 1'h0 : _T_37; // @[Lookup.scala 33:37]
  wire  _T_39 = _T_7 ? 1'h0 : _T_38; // @[Lookup.scala 33:37]
  wire  _T_40 = _T_5 ? 1'h0 : _T_39; // @[Lookup.scala 33:37]
  wire  _T_41 = _T_3 ? 1'h0 : _T_40; // @[Lookup.scala 33:37]
  wire  _T_45 = _T_11 ? 1'h0 : _T_13; // @[Lookup.scala 33:37]
  wire  _T_46 = _T_9 ? 1'h0 : _T_45; // @[Lookup.scala 33:37]
  wire  _T_55 = _T_7 ? 1'h0 : _T_9; // @[Lookup.scala 33:37]
  wire  _T_56 = _T_5 ? 1'h0 : _T_55; // @[Lookup.scala 33:37]
  wire  _T_57 = _T_3 ? 1'h0 : _T_56; // @[Lookup.scala 33:37]
  wire  _T_60 = _T_13 ? 1'h0 : _T_15; // @[Lookup.scala 33:37]
  wire  _T_61 = _T_11 ? 1'h0 : _T_60; // @[Lookup.scala 33:37]
  wire  _T_62 = _T_9 ? 1'h0 : _T_61; // @[Lookup.scala 33:37]
  wire  _T_63 = _T_7 ? 1'h0 : _T_62; // @[Lookup.scala 33:37]
  wire  _T_64 = _T_5 ? 1'h0 : _T_63; // @[Lookup.scala 33:37]
  wire  _T_65 = _T_3 ? 1'h0 : _T_64; // @[Lookup.scala 33:37]
  wire  _T_67 = _T_15 ? 1'h0 : _T_17; // @[Lookup.scala 33:37]
  wire  _T_68 = _T_13 ? 1'h0 : _T_67; // @[Lookup.scala 33:37]
  wire  _T_69 = _T_11 ? 1'h0 : _T_68; // @[Lookup.scala 33:37]
  wire  _T_70 = _T_9 ? 1'h0 : _T_69; // @[Lookup.scala 33:37]
  wire  _T_71 = _T_7 ? 1'h0 : _T_70; // @[Lookup.scala 33:37]
  wire  _T_72 = _T_5 ? 1'h0 : _T_71; // @[Lookup.scala 33:37]
  wire  _T_73 = _T_3 ? 1'h0 : _T_72; // @[Lookup.scala 33:37]
  wire  _T_76 = _T_13 ? 1'h0 : _T_15 | _T_17; // @[Lookup.scala 33:37]
  wire  _T_77 = _T_11 ? 1'h0 : _T_76; // @[Lookup.scala 33:37]
  wire  _T_78 = _T_9 ? 1'h0 : _T_77; // @[Lookup.scala 33:37]
  wire  _T_79 = _T_7 ? 1'h0 : _T_78; // @[Lookup.scala 33:37]
  wire  _T_80 = _T_5 ? 1'h0 : _T_79; // @[Lookup.scala 33:37]
  wire  _T_81 = _T_3 ? 1'h0 : _T_80; // @[Lookup.scala 33:37]
  wire  _T_86 = _T_9 ? 1'h0 : _T_11; // @[Lookup.scala 33:37]
  wire  _T_87 = _T_7 ? 1'h0 : _T_86; // @[Lookup.scala 33:37]
  wire  _T_88 = _T_5 ? 1'h0 : _T_87; // @[Lookup.scala 33:37]
  wire  _T_89 = _T_3 ? 1'h0 : _T_88; // @[Lookup.scala 33:37]
  wire [1:0] _T_95 = _T_7 ? 2'h3 : 2'h0; // @[Lookup.scala 33:37]
  wire [1:0] _T_96 = _T_5 ? 2'h2 : _T_95; // @[Lookup.scala 33:37]
  wire [1:0] _T_97 = _T_3 ? 2'h0 : _T_96; // @[Lookup.scala 33:37]
  wire  _T_105 = _T_3 ? 1'h0 : _T_5; // @[Lookup.scala 33:37]
  wire  _T_110 = _T_9 ? 1'h0 : _T_11 | (_T_13 | (_T_15 | _T_17)); // @[Lookup.scala 33:37]
  wire  _T_111 = _T_7 ? 1'h0 : _T_110; // @[Lookup.scala 33:37]
  assign io_itype = _T_1 ? 1'h0 : _T_3; // @[Lookup.scala 33:37]
  assign io_aluop = _T_1 | _T_3; // @[Lookup.scala 33:37]
  assign io_xsrc = _T_1 ? 1'h0 : _T_41; // @[Lookup.scala 33:37]
  assign io_ysrc = _T_1 ? 1'h0 : _T_3 | (_T_5 | (_T_7 | _T_46)); // @[Lookup.scala 33:37]
  assign io_branch = _T_1 ? 1'h0 : _T_57; // @[Lookup.scala 33:37]
  assign io_jal = _T_1 ? 1'h0 : _T_65; // @[Lookup.scala 33:37]
  assign io_jalr = _T_1 ? 1'h0 : _T_73; // @[Lookup.scala 33:37]
  assign io_plus4 = _T_1 ? 1'h0 : _T_81; // @[Lookup.scala 33:37]
  assign io_resultselect = _T_1 ? 1'h0 : _T_89; // @[Lookup.scala 33:37]
  assign io_memop = _T_1 ? 2'h0 : _T_97; // @[Lookup.scala 33:37]
  assign io_toreg = _T_1 ? 1'h0 : _T_105; // @[Lookup.scala 33:37]
  assign io_regwrite = _T_1 | (_T_3 | (_T_5 | _T_111)); // @[Lookup.scala 33:37]
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
  reg [31:0] regs_0; // @[register-file.scala 52:17]
  reg [31:0] regs_1; // @[register-file.scala 52:17]
  reg [31:0] regs_2; // @[register-file.scala 52:17]
  reg [31:0] regs_3; // @[register-file.scala 52:17]
  reg [31:0] regs_4; // @[register-file.scala 52:17]
  reg [31:0] regs_5; // @[register-file.scala 52:17]
  reg [31:0] regs_6; // @[register-file.scala 52:17]
  reg [31:0] regs_7; // @[register-file.scala 52:17]
  reg [31:0] regs_8; // @[register-file.scala 52:17]
  reg [31:0] regs_9; // @[register-file.scala 52:17]
  reg [31:0] regs_10; // @[register-file.scala 52:17]
  reg [31:0] regs_11; // @[register-file.scala 52:17]
  reg [31:0] regs_12; // @[register-file.scala 52:17]
  reg [31:0] regs_13; // @[register-file.scala 52:17]
  reg [31:0] regs_14; // @[register-file.scala 52:17]
  reg [31:0] regs_15; // @[register-file.scala 52:17]
  reg [31:0] regs_16; // @[register-file.scala 52:17]
  reg [31:0] regs_17; // @[register-file.scala 52:17]
  reg [31:0] regs_18; // @[register-file.scala 52:17]
  reg [31:0] regs_19; // @[register-file.scala 52:17]
  reg [31:0] regs_20; // @[register-file.scala 52:17]
  reg [31:0] regs_21; // @[register-file.scala 52:17]
  reg [31:0] regs_22; // @[register-file.scala 52:17]
  reg [31:0] regs_23; // @[register-file.scala 52:17]
  reg [31:0] regs_24; // @[register-file.scala 52:17]
  reg [31:0] regs_25; // @[register-file.scala 52:17]
  reg [31:0] regs_26; // @[register-file.scala 52:17]
  reg [31:0] regs_27; // @[register-file.scala 52:17]
  reg [31:0] regs_28; // @[register-file.scala 52:17]
  reg [31:0] regs_29; // @[register-file.scala 52:17]
  reg [31:0] regs_30; // @[register-file.scala 52:17]
  reg [31:0] regs_31; // @[register-file.scala 52:17]
  wire [31:0] _GEN_65 = 5'h1 == io_readreg1 ? regs_1 : regs_0; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_66 = 5'h2 == io_readreg1 ? regs_2 : _GEN_65; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_67 = 5'h3 == io_readreg1 ? regs_3 : _GEN_66; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_68 = 5'h4 == io_readreg1 ? regs_4 : _GEN_67; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_69 = 5'h5 == io_readreg1 ? regs_5 : _GEN_68; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_70 = 5'h6 == io_readreg1 ? regs_6 : _GEN_69; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_71 = 5'h7 == io_readreg1 ? regs_7 : _GEN_70; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_72 = 5'h8 == io_readreg1 ? regs_8 : _GEN_71; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_73 = 5'h9 == io_readreg1 ? regs_9 : _GEN_72; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_74 = 5'ha == io_readreg1 ? regs_10 : _GEN_73; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_75 = 5'hb == io_readreg1 ? regs_11 : _GEN_74; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_76 = 5'hc == io_readreg1 ? regs_12 : _GEN_75; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_77 = 5'hd == io_readreg1 ? regs_13 : _GEN_76; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_78 = 5'he == io_readreg1 ? regs_14 : _GEN_77; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_79 = 5'hf == io_readreg1 ? regs_15 : _GEN_78; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_80 = 5'h10 == io_readreg1 ? regs_16 : _GEN_79; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_81 = 5'h11 == io_readreg1 ? regs_17 : _GEN_80; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_82 = 5'h12 == io_readreg1 ? regs_18 : _GEN_81; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_83 = 5'h13 == io_readreg1 ? regs_19 : _GEN_82; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_84 = 5'h14 == io_readreg1 ? regs_20 : _GEN_83; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_85 = 5'h15 == io_readreg1 ? regs_21 : _GEN_84; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_86 = 5'h16 == io_readreg1 ? regs_22 : _GEN_85; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_87 = 5'h17 == io_readreg1 ? regs_23 : _GEN_86; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_88 = 5'h18 == io_readreg1 ? regs_24 : _GEN_87; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_89 = 5'h19 == io_readreg1 ? regs_25 : _GEN_88; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_90 = 5'h1a == io_readreg1 ? regs_26 : _GEN_89; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_91 = 5'h1b == io_readreg1 ? regs_27 : _GEN_90; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_92 = 5'h1c == io_readreg1 ? regs_28 : _GEN_91; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_93 = 5'h1d == io_readreg1 ? regs_29 : _GEN_92; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_94 = 5'h1e == io_readreg1 ? regs_30 : _GEN_93; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_95 = 5'h1f == io_readreg1 ? regs_31 : _GEN_94; // @[register-file.scala 61:{16,16}]
  wire [31:0] _GEN_97 = 5'h1 == io_readreg2 ? regs_1 : regs_0; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_98 = 5'h2 == io_readreg2 ? regs_2 : _GEN_97; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_99 = 5'h3 == io_readreg2 ? regs_3 : _GEN_98; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_100 = 5'h4 == io_readreg2 ? regs_4 : _GEN_99; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_101 = 5'h5 == io_readreg2 ? regs_5 : _GEN_100; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_102 = 5'h6 == io_readreg2 ? regs_6 : _GEN_101; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_103 = 5'h7 == io_readreg2 ? regs_7 : _GEN_102; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_104 = 5'h8 == io_readreg2 ? regs_8 : _GEN_103; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_105 = 5'h9 == io_readreg2 ? regs_9 : _GEN_104; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_106 = 5'ha == io_readreg2 ? regs_10 : _GEN_105; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_107 = 5'hb == io_readreg2 ? regs_11 : _GEN_106; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_108 = 5'hc == io_readreg2 ? regs_12 : _GEN_107; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_109 = 5'hd == io_readreg2 ? regs_13 : _GEN_108; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_110 = 5'he == io_readreg2 ? regs_14 : _GEN_109; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_111 = 5'hf == io_readreg2 ? regs_15 : _GEN_110; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_112 = 5'h10 == io_readreg2 ? regs_16 : _GEN_111; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_113 = 5'h11 == io_readreg2 ? regs_17 : _GEN_112; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_114 = 5'h12 == io_readreg2 ? regs_18 : _GEN_113; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_115 = 5'h13 == io_readreg2 ? regs_19 : _GEN_114; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_116 = 5'h14 == io_readreg2 ? regs_20 : _GEN_115; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_117 = 5'h15 == io_readreg2 ? regs_21 : _GEN_116; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_118 = 5'h16 == io_readreg2 ? regs_22 : _GEN_117; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_119 = 5'h17 == io_readreg2 ? regs_23 : _GEN_118; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_120 = 5'h18 == io_readreg2 ? regs_24 : _GEN_119; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_121 = 5'h19 == io_readreg2 ? regs_25 : _GEN_120; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_122 = 5'h1a == io_readreg2 ? regs_26 : _GEN_121; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_123 = 5'h1b == io_readreg2 ? regs_27 : _GEN_122; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_124 = 5'h1c == io_readreg2 ? regs_28 : _GEN_123; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_125 = 5'h1d == io_readreg2 ? regs_29 : _GEN_124; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_126 = 5'h1e == io_readreg2 ? regs_30 : _GEN_125; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_127 = 5'h1f == io_readreg2 ? regs_31 : _GEN_126; // @[register-file.scala 62:{16,16}]
  wire [31:0] _GEN_128 = io_readreg2 == io_writereg & io_wen ? io_writedata : _GEN_127; // @[register-file.scala 62:16 68:57 69:20]
  assign io_readdata1 = io_readreg1 == io_writereg & io_wen ? io_writedata : _GEN_95; // @[register-file.scala 61:16 66:50 67:20]
  assign io_readdata2 = io_readreg1 == io_writereg & io_wen ? _GEN_127 : _GEN_128; // @[register-file.scala 62:16 66:50]
  always @(posedge clock) begin
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h0 == io_writereg) begin // @[register-file.scala 56:23]
        regs_0 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h1 == io_writereg) begin // @[register-file.scala 56:23]
        regs_1 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h2 == io_writereg) begin // @[register-file.scala 56:23]
        regs_2 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h3 == io_writereg) begin // @[register-file.scala 56:23]
        regs_3 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h4 == io_writereg) begin // @[register-file.scala 56:23]
        regs_4 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h5 == io_writereg) begin // @[register-file.scala 56:23]
        regs_5 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h6 == io_writereg) begin // @[register-file.scala 56:23]
        regs_6 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h7 == io_writereg) begin // @[register-file.scala 56:23]
        regs_7 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h8 == io_writereg) begin // @[register-file.scala 56:23]
        regs_8 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h9 == io_writereg) begin // @[register-file.scala 56:23]
        regs_9 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'ha == io_writereg) begin // @[register-file.scala 56:23]
        regs_10 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'hb == io_writereg) begin // @[register-file.scala 56:23]
        regs_11 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'hc == io_writereg) begin // @[register-file.scala 56:23]
        regs_12 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'hd == io_writereg) begin // @[register-file.scala 56:23]
        regs_13 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'he == io_writereg) begin // @[register-file.scala 56:23]
        regs_14 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'hf == io_writereg) begin // @[register-file.scala 56:23]
        regs_15 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h10 == io_writereg) begin // @[register-file.scala 56:23]
        regs_16 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h11 == io_writereg) begin // @[register-file.scala 56:23]
        regs_17 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h12 == io_writereg) begin // @[register-file.scala 56:23]
        regs_18 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h13 == io_writereg) begin // @[register-file.scala 56:23]
        regs_19 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h14 == io_writereg) begin // @[register-file.scala 56:23]
        regs_20 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h15 == io_writereg) begin // @[register-file.scala 56:23]
        regs_21 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h16 == io_writereg) begin // @[register-file.scala 56:23]
        regs_22 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h17 == io_writereg) begin // @[register-file.scala 56:23]
        regs_23 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h18 == io_writereg) begin // @[register-file.scala 56:23]
        regs_24 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h19 == io_writereg) begin // @[register-file.scala 56:23]
        regs_25 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h1a == io_writereg) begin // @[register-file.scala 56:23]
        regs_26 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h1b == io_writereg) begin // @[register-file.scala 56:23]
        regs_27 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h1c == io_writereg) begin // @[register-file.scala 56:23]
        regs_28 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h1d == io_writereg) begin // @[register-file.scala 56:23]
        regs_29 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h1e == io_writereg) begin // @[register-file.scala 56:23]
        regs_30 <= io_writedata; // @[register-file.scala 56:23]
      end
    end
    if (io_wen) begin // @[register-file.scala 55:17]
      if (5'h1f == io_writereg) begin // @[register-file.scala 56:23]
        regs_31 <= io_writedata; // @[register-file.scala 56:23]
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
module ALUControl(
  input        io_aluop,
  input        io_itype,
  input  [6:0] io_funct7,
  input  [2:0] io_funct3,
  output [3:0] io_operation
);
  wire  _T_1 = io_funct7 == 7'h0; // @[alucontrol.scala 32:35]
  wire [2:0] _GEN_0 = io_itype | io_funct7 == 7'h0 ? 3'h7 : 3'h4; // @[alucontrol.scala 32:53 33:22 35:22]
  wire [1:0] _GEN_1 = _T_1 ? 2'h2 : 2'h3; // @[alucontrol.scala 43:41 44:22 46:22]
  wire [2:0] _GEN_2 = io_funct3 == 3'h6 ? 3'h5 : 3'h6; // @[alucontrol.scala 49:{40,55} 51:20]
  wire [2:0] _GEN_3 = io_funct3 == 3'h5 ? {{1'd0}, _GEN_1} : _GEN_2; // @[alucontrol.scala 42:40]
  wire [2:0] _GEN_4 = io_funct3 == 3'h4 ? 3'h0 : _GEN_3; // @[alucontrol.scala 41:{40,55}]
  wire [2:0] _GEN_5 = io_funct3 == 3'h3 ? 3'h1 : _GEN_4; // @[alucontrol.scala 40:{40,55}]
  wire [3:0] _GEN_6 = io_funct3 == 3'h2 ? 4'h9 : {{1'd0}, _GEN_5}; // @[alucontrol.scala 39:{40,55}]
  wire [3:0] _GEN_7 = io_funct3 == 3'h1 ? 4'h8 : _GEN_6; // @[alucontrol.scala 38:{40,55}]
  wire [3:0] _GEN_8 = io_funct3 == 3'h0 ? {{1'd0}, _GEN_0} : _GEN_7; // @[alucontrol.scala 31:35]
  assign io_operation = io_aluop ? _GEN_8 : 4'h7; // @[alucontrol.scala 30:19 53:18]
endmodule
module ALU(
  input  [3:0]  io_operation,
  input  [31:0] io_inputx,
  input  [31:0] io_inputy,
  output [31:0] io_result
);
  wire [31:0] _T_1 = io_inputx & io_inputy; // @[alu.scala 26:28]
  wire [31:0] _T_3 = io_inputx | io_inputy; // @[alu.scala 29:28]
  wire [31:0] _T_6 = io_inputx + io_inputy; // @[alu.scala 32:28]
  wire [31:0] _T_9 = io_inputx - io_inputy; // @[alu.scala 35:28]
  wire [31:0] _T_11 = io_inputx; // @[alu.scala 38:29]
  wire [31:0] _T_14 = $signed(io_inputx) >>> io_inputy[4:0]; // @[alu.scala 38:55]
  wire [31:0] _T_18 = io_inputx ^ io_inputy; // @[alu.scala 44:28]
  wire [31:0] _T_21 = io_inputx >> io_inputy[4:0]; // @[alu.scala 47:28]
  wire [31:0] _T_24 = io_inputy; // @[alu.scala 50:48]
  wire [62:0] _GEN_15 = {{31'd0}, io_inputx}; // @[alu.scala 53:28]
  wire [62:0] _T_28 = _GEN_15 << io_inputy[4:0]; // @[alu.scala 53:28]
  wire [31:0] _T_31 = ~_T_3; // @[alu.scala 56:18]
  wire  _GEN_0 = io_operation == 4'he & io_inputx != io_inputy; // @[alu.scala 67:42 68:15 71:15]
  wire  _GEN_1 = io_operation == 4'hd ? io_inputx == io_inputy : _GEN_0; // @[alu.scala 64:42 65:15]
  wire  _GEN_2 = io_operation == 4'hc ? io_inputx >= io_inputy : _GEN_1; // @[alu.scala 61:42 62:15]
  wire  _GEN_3 = io_operation == 4'hb ? $signed(io_inputx) >= $signed(io_inputy) : _GEN_2; // @[alu.scala 58:42 59:15]
  wire [31:0] _GEN_4 = io_operation == 4'ha ? _T_31 : {{31'd0}, _GEN_3}; // @[alu.scala 55:42 56:15]
  wire [62:0] _GEN_5 = io_operation == 4'h8 ? _T_28 : {{31'd0}, _GEN_4}; // @[alu.scala 52:42 53:15]
  wire [62:0] _GEN_6 = io_operation == 4'h9 ? {{62'd0}, $signed(_T_11) < $signed(_T_24)} : _GEN_5; // @[alu.scala 49:42 50:15]
  wire [62:0] _GEN_7 = io_operation == 4'h2 ? {{31'd0}, _T_21} : _GEN_6; // @[alu.scala 46:42 47:15]
  wire [62:0] _GEN_8 = io_operation == 4'h0 ? {{31'd0}, _T_18} : _GEN_7; // @[alu.scala 43:42 44:15]
  wire [62:0] _GEN_9 = io_operation == 4'h1 ? {{62'd0}, io_inputx < io_inputy} : _GEN_8; // @[alu.scala 40:42 41:15]
  wire [62:0] _GEN_10 = io_operation == 4'h3 ? {{31'd0}, _T_14} : _GEN_9; // @[alu.scala 37:42 38:15]
  wire [62:0] _GEN_11 = io_operation == 4'h4 ? {{31'd0}, _T_9} : _GEN_10; // @[alu.scala 34:42 35:15]
  wire [62:0] _GEN_12 = io_operation == 4'h7 ? {{31'd0}, _T_6} : _GEN_11; // @[alu.scala 31:42 32:15]
  wire [62:0] _GEN_13 = io_operation == 4'h5 ? {{31'd0}, _T_3} : _GEN_12; // @[alu.scala 28:42 29:15]
  wire [62:0] _GEN_14 = io_operation == 4'h6 ? {{31'd0}, _T_1} : _GEN_13; // @[alu.scala 25:37 26:15]
  assign io_result = _GEN_14[31:0];
endmodule
module ImmediateGenerator(
  input  [31:0] io_instruction,
  output [31:0] io_sextImm
);
  wire [6:0] opcode = io_instruction[6:0]; // @[helpers.scala 44:30]
  wire  _T = 7'h37 == opcode; // @[Conditional.scala 37:30]
  wire [31:0] _T_3 = {io_instruction[31:12],12'h0}; // @[Cat.scala 30:58]
  wire  _T_4 = 7'h17 == opcode; // @[Conditional.scala 37:30]
  wire  _T_8 = 7'h6f == opcode; // @[Conditional.scala 37:30]
  wire [19:0] _T_15 = {io_instruction[31],io_instruction[19:12],io_instruction[20],io_instruction[30:21]}; // @[Cat.scala 30:58]
  wire [10:0] _T_18 = _T_15[19] ? 11'h7ff : 11'h0; // @[Bitwise.scala 72:12]
  wire [31:0] _T_20 = {_T_18,io_instruction[31],io_instruction[19:12],io_instruction[20],io_instruction[30:21],1'h0}; // @[Cat.scala 30:58]
  wire  _T_21 = 7'h67 == opcode; // @[Conditional.scala 37:30]
  wire [19:0] _T_25 = io_instruction[31] ? 20'hfffff : 20'h0; // @[Bitwise.scala 72:12]
  wire [31:0] _T_26 = {_T_25,io_instruction[31:20]}; // @[Cat.scala 30:58]
  wire  _T_27 = 7'h63 == opcode; // @[Conditional.scala 37:30]
  wire [11:0] _T_34 = {io_instruction[31],io_instruction[7],io_instruction[30:25],io_instruction[11:8]}; // @[Cat.scala 30:58]
  wire [18:0] _T_37 = _T_34[11] ? 19'h7ffff : 19'h0; // @[Bitwise.scala 72:12]
  wire [31:0] _T_39 = {_T_37,io_instruction[31],io_instruction[7],io_instruction[30:25],io_instruction[11:8],1'h0}; // @[Cat.scala 30:58]
  wire  _T_40 = 7'h3 == opcode; // @[Conditional.scala 37:30]
  wire  _T_46 = 7'h23 == opcode; // @[Conditional.scala 37:30]
  wire [11:0] _T_49 = {io_instruction[31:25],io_instruction[11:7]}; // @[Cat.scala 30:58]
  wire [19:0] _T_52 = _T_49[11] ? 20'hfffff : 20'h0; // @[Bitwise.scala 72:12]
  wire [31:0] _T_53 = {_T_52,io_instruction[31:25],io_instruction[11:7]}; // @[Cat.scala 30:58]
  wire  _T_54 = 7'h13 == opcode; // @[Conditional.scala 37:30]
  wire  _T_60 = 7'h73 == opcode; // @[Conditional.scala 37:30]
  wire [31:0] _T_63 = {27'h0,io_instruction[19:15]}; // @[Cat.scala 30:58]
  wire [31:0] _GEN_0 = _T_60 ? _T_63 : 32'h0; // @[Conditional.scala 39:67 helpers.scala 42:14 81:18]
  wire [31:0] _GEN_1 = _T_54 ? _T_26 : _GEN_0; // @[Conditional.scala 39:67 helpers.scala 78:18]
  wire [31:0] _GEN_2 = _T_46 ? _T_53 : _GEN_1; // @[Conditional.scala 39:67 helpers.scala 74:18]
  wire [31:0] _GEN_3 = _T_40 ? _T_26 : _GEN_2; // @[Conditional.scala 39:67 helpers.scala 70:18]
  wire [31:0] _GEN_4 = _T_27 ? _T_39 : _GEN_3; // @[Conditional.scala 39:67 helpers.scala 66:18]
  wire [31:0] _GEN_5 = _T_21 ? _T_26 : _GEN_4; // @[Conditional.scala 39:67 helpers.scala 61:18]
  wire [31:0] _GEN_6 = _T_8 ? _T_20 : _GEN_5; // @[Conditional.scala 39:67 helpers.scala 57:18]
  wire [31:0] _GEN_7 = _T_4 ? _T_3 : _GEN_6; // @[Conditional.scala 39:67 helpers.scala 52:18]
  assign io_sextImm = _T ? _T_3 : _GEN_7; // @[Conditional.scala 40:58 helpers.scala 48:18]
endmodule
module Adder(
  input  [31:0] io_inputx,
  input  [31:0] io_inputy,
  output [31:0] io_result
);
  assign io_result = io_inputx + io_inputy; // @[helpers.scala 23:26]
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
  wire  _GEN_0 = io_funct3 == 3'h7 & io_inputx >= io_inputy; // @[nextpc.scala 44:{40,51} 45:51]
  wire  _GEN_1 = io_funct3 == 3'h6 ? io_inputx < io_inputy : _GEN_0; // @[nextpc.scala 43:{40,51}]
  wire  _GEN_2 = io_funct3 == 3'h5 ? $signed(io_inputx) >= $signed(io_inputy) : _GEN_1; // @[nextpc.scala 42:{40,51}]
  wire  _GEN_3 = io_funct3 == 3'h4 ? $signed(io_inputx) < $signed(io_inputy) : _GEN_2; // @[nextpc.scala 41:{40,51}]
  wire  _GEN_4 = io_funct3 == 3'h1 ? io_inputx != io_inputy : _GEN_3; // @[nextpc.scala 40:{40,51}]
  wire  _GEN_5 = io_funct3 == 3'h0 ? io_inputx == io_inputy : _GEN_4; // @[nextpc.scala 39:{40,51}]
  wire [31:0] _T_17 = io_pc + io_imm; // @[nextpc.scala 48:26]
  wire [31:0] _T_19 = io_pc + 32'h4; // @[nextpc.scala 50:26]
  wire [31:0] _GEN_6 = io_taken ? _T_17 : _T_19; // @[nextpc.scala 47:21 48:17 50:17]
  wire [31:0] _T_23 = io_inputx + io_imm; // @[nextpc.scala 57:28]
  wire [31:0] _GEN_8 = io_jalr ? _T_23 : _T_19; // @[nextpc.scala 55:25 57:15 59:15]
  wire  _GEN_9 = io_jal | io_jalr; // @[nextpc.scala 52:24 53:14]
  wire [31:0] _GEN_10 = io_jal ? _T_17 : _GEN_8; // @[nextpc.scala 52:24 54:15]
  assign io_nextpc = io_branch ? _GEN_6 : _GEN_10; // @[nextpc.scala 38:20]
  assign io_taken = io_branch ? _GEN_5 : _GEN_9; // @[nextpc.scala 38:20]
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
  wire  _T_3 = io_exmemrd != 5'h0; // @[forwarding.scala 40:70]
  wire  _T_8 = io_memwbrd != 5'h0; // @[forwarding.scala 43:75]
  wire [1:0] _GEN_0 = io_memwbrw & io_memwbrd == io_rs1 & io_memwbrd != 5'h0 ? 2'h2 : 2'h0; // @[forwarding.scala 43:84 44:17 47:17]
  wire [1:0] _GEN_2 = io_memwbrw & io_memwbrd == io_rs2 & _T_8 ? 2'h2 : 2'h0; // @[forwarding.scala 53:84 54:17 57:17]
  assign io_forwardA = io_exmemrw & io_exmemrd == io_rs1 & io_exmemrd != 5'h0 ? 2'h1 : _GEN_0; // @[forwarding.scala 40:79 41:17]
  assign io_forwardB = io_exmemrw & io_exmemrd == io_rs2 & _T_3 ? 2'h1 : _GEN_2; // @[forwarding.scala 50:79 51:17]
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
  wire  _T_4 = io_idex_rd == io_rs1 | io_idex_rd == io_rs2; // @[hazard-bp.scala 56:32]
  wire  _T_5 = io_idex_memread & _T_4; // @[hazard-bp.scala 55:41]
  wire [1:0] _GEN_3 = _T_5 ? 2'h3 : 2'h0; // @[hazard-bp.scala 56:59 58:14]
  assign io_pcSel = io_exmem_taken ? 2'h1 : _GEN_3; // @[hazard-bp.scala 48:36 50:14]
  assign io_if_id_stall = io_exmem_taken ? 1'h0 : _T_5; // @[hazard-bp.scala 43:19 48:36]
  assign io_if_id_flush = io_exmem_taken; // @[hazard-bp.scala 48:36 51:21]
  assign io_id_ex_flush = io_exmem_taken | _T_5; // @[hazard-bp.scala 48:36 52:21]
  assign io_ex_mem_flush = io_exmem_taken; // @[hazard-bp.scala 48:36 53:21]
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
  reg [31:0] reg_instruction; // @[stage-register.scala 43:21]
  reg [31:0] reg_pc; // @[stage-register.scala 43:21]
  assign io_data_instruction = reg_instruction; // @[stage-register.scala 45:11]
  assign io_data_pc = reg_pc; // @[stage-register.scala 45:11]
  always @(posedge clock) begin
    if (reset) begin // @[stage-register.scala 43:21]
      reg_instruction <= 32'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_instruction <= 32'h0; // @[stage-register.scala 52:9]
    end else if (io_valid) begin // @[stage-register.scala 47:19]
      reg_instruction <= io_in_instruction; // @[stage-register.scala 48:9]
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_pc <= 32'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_pc <= 32'h0; // @[stage-register.scala 52:9]
    end else if (io_valid) begin // @[stage-register.scala 47:19]
      reg_pc <= io_in_pc; // @[stage-register.scala 48:9]
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
  reg [31:0] reg_pc; // @[stage-register.scala 43:21]
  reg [31:0] reg_instruction; // @[stage-register.scala 43:21]
  reg [31:0] reg_sextImm; // @[stage-register.scala 43:21]
  reg [31:0] reg_readdata1; // @[stage-register.scala 43:21]
  reg [31:0] reg_readdata2; // @[stage-register.scala 43:21]
  assign io_data_pc = reg_pc; // @[stage-register.scala 45:11]
  assign io_data_instruction = reg_instruction; // @[stage-register.scala 45:11]
  assign io_data_sextImm = reg_sextImm; // @[stage-register.scala 45:11]
  assign io_data_readdata1 = reg_readdata1; // @[stage-register.scala 45:11]
  assign io_data_readdata2 = reg_readdata2; // @[stage-register.scala 45:11]
  always @(posedge clock) begin
    if (reset) begin // @[stage-register.scala 43:21]
      reg_pc <= 32'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_pc <= 32'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_pc <= io_in_pc;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_instruction <= 32'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_instruction <= 32'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_instruction <= io_in_instruction;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_sextImm <= 32'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_sextImm <= 32'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_sextImm <= io_in_sextImm;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_readdata1 <= 32'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_readdata1 <= 32'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_readdata1 <= io_in_readdata1;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_readdata2 <= 32'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_readdata2 <= 32'h0; // @[stage-register.scala 52:9]
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
  reg  reg_ex_ctrl_itype; // @[stage-register.scala 43:21]
  reg  reg_ex_ctrl_aluop; // @[stage-register.scala 43:21]
  reg  reg_ex_ctrl_resultselect; // @[stage-register.scala 43:21]
  reg  reg_ex_ctrl_xsrc; // @[stage-register.scala 43:21]
  reg  reg_ex_ctrl_ysrc; // @[stage-register.scala 43:21]
  reg  reg_ex_ctrl_plus4; // @[stage-register.scala 43:21]
  reg  reg_ex_ctrl_branch; // @[stage-register.scala 43:21]
  reg  reg_ex_ctrl_jal; // @[stage-register.scala 43:21]
  reg  reg_ex_ctrl_jalr; // @[stage-register.scala 43:21]
  reg [1:0] reg_mem_ctrl_memop; // @[stage-register.scala 43:21]
  reg  reg_wb_ctrl_toreg; // @[stage-register.scala 43:21]
  reg  reg_wb_ctrl_regwrite; // @[stage-register.scala 43:21]
  assign io_data_ex_ctrl_itype = reg_ex_ctrl_itype; // @[stage-register.scala 45:11]
  assign io_data_ex_ctrl_aluop = reg_ex_ctrl_aluop; // @[stage-register.scala 45:11]
  assign io_data_ex_ctrl_resultselect = reg_ex_ctrl_resultselect; // @[stage-register.scala 45:11]
  assign io_data_ex_ctrl_xsrc = reg_ex_ctrl_xsrc; // @[stage-register.scala 45:11]
  assign io_data_ex_ctrl_ysrc = reg_ex_ctrl_ysrc; // @[stage-register.scala 45:11]
  assign io_data_ex_ctrl_plus4 = reg_ex_ctrl_plus4; // @[stage-register.scala 45:11]
  assign io_data_ex_ctrl_branch = reg_ex_ctrl_branch; // @[stage-register.scala 45:11]
  assign io_data_ex_ctrl_jal = reg_ex_ctrl_jal; // @[stage-register.scala 45:11]
  assign io_data_ex_ctrl_jalr = reg_ex_ctrl_jalr; // @[stage-register.scala 45:11]
  assign io_data_mem_ctrl_memop = reg_mem_ctrl_memop; // @[stage-register.scala 45:11]
  assign io_data_wb_ctrl_toreg = reg_wb_ctrl_toreg; // @[stage-register.scala 45:11]
  assign io_data_wb_ctrl_regwrite = reg_wb_ctrl_regwrite; // @[stage-register.scala 45:11]
  always @(posedge clock) begin
    if (reset) begin // @[stage-register.scala 43:21]
      reg_ex_ctrl_itype <= 1'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_ex_ctrl_itype <= 1'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_ex_ctrl_itype <= io_in_ex_ctrl_itype;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_ex_ctrl_aluop <= 1'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_ex_ctrl_aluop <= 1'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_ex_ctrl_aluop <= io_in_ex_ctrl_aluop;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_ex_ctrl_resultselect <= 1'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_ex_ctrl_resultselect <= 1'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_ex_ctrl_resultselect <= io_in_ex_ctrl_resultselect;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_ex_ctrl_xsrc <= 1'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_ex_ctrl_xsrc <= 1'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_ex_ctrl_xsrc <= io_in_ex_ctrl_xsrc;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_ex_ctrl_ysrc <= 1'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_ex_ctrl_ysrc <= 1'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_ex_ctrl_ysrc <= io_in_ex_ctrl_ysrc;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_ex_ctrl_plus4 <= 1'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_ex_ctrl_plus4 <= 1'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_ex_ctrl_plus4 <= io_in_ex_ctrl_plus4;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_ex_ctrl_branch <= 1'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_ex_ctrl_branch <= 1'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_ex_ctrl_branch <= io_in_ex_ctrl_branch;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_ex_ctrl_jal <= 1'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_ex_ctrl_jal <= 1'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_ex_ctrl_jal <= io_in_ex_ctrl_jal;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_ex_ctrl_jalr <= 1'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_ex_ctrl_jalr <= 1'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_ex_ctrl_jalr <= io_in_ex_ctrl_jalr;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_mem_ctrl_memop <= 2'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_mem_ctrl_memop <= 2'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_mem_ctrl_memop <= io_in_mem_ctrl_memop;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_wb_ctrl_toreg <= 1'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_wb_ctrl_toreg <= 1'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_wb_ctrl_toreg <= io_in_wb_ctrl_toreg;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_wb_ctrl_regwrite <= 1'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_wb_ctrl_regwrite <= 1'h0; // @[stage-register.scala 52:9]
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
  reg [31:0] reg_ex_result; // @[stage-register.scala 43:21]
  reg [31:0] reg_mem_writedata; // @[stage-register.scala 43:21]
  reg [31:0] reg_instruction; // @[stage-register.scala 43:21]
  reg [31:0] reg_next_pc; // @[stage-register.scala 43:21]
  reg  reg_taken; // @[stage-register.scala 43:21]
  assign io_data_ex_result = reg_ex_result; // @[stage-register.scala 45:11]
  assign io_data_mem_writedata = reg_mem_writedata; // @[stage-register.scala 45:11]
  assign io_data_instruction = reg_instruction; // @[stage-register.scala 45:11]
  assign io_data_next_pc = reg_next_pc; // @[stage-register.scala 45:11]
  assign io_data_taken = reg_taken; // @[stage-register.scala 45:11]
  always @(posedge clock) begin
    if (reset) begin // @[stage-register.scala 43:21]
      reg_ex_result <= 32'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_ex_result <= 32'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_ex_result <= io_in_ex_result;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_mem_writedata <= 32'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_mem_writedata <= 32'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_mem_writedata <= io_in_mem_writedata;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_instruction <= 32'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_instruction <= 32'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_instruction <= io_in_instruction;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_next_pc <= 32'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_next_pc <= 32'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_next_pc <= io_in_next_pc;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_taken <= 1'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_taken <= 1'h0; // @[stage-register.scala 52:9]
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
  reg [1:0] reg_mem_ctrl_memop; // @[stage-register.scala 43:21]
  reg  reg_wb_ctrl_toreg; // @[stage-register.scala 43:21]
  reg  reg_wb_ctrl_regwrite; // @[stage-register.scala 43:21]
  assign io_data_mem_ctrl_memop = reg_mem_ctrl_memop; // @[stage-register.scala 45:11]
  assign io_data_wb_ctrl_toreg = reg_wb_ctrl_toreg; // @[stage-register.scala 45:11]
  assign io_data_wb_ctrl_regwrite = reg_wb_ctrl_regwrite; // @[stage-register.scala 45:11]
  always @(posedge clock) begin
    if (reset) begin // @[stage-register.scala 43:21]
      reg_mem_ctrl_memop <= 2'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_mem_ctrl_memop <= 2'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_mem_ctrl_memop <= io_in_mem_ctrl_memop;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_wb_ctrl_toreg <= 1'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_wb_ctrl_toreg <= 1'h0; // @[stage-register.scala 52:9]
    end else begin
      reg_wb_ctrl_toreg <= io_in_wb_ctrl_toreg;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_wb_ctrl_regwrite <= 1'h0; // @[stage-register.scala 43:21]
    end else if (io_flush) begin // @[stage-register.scala 51:19]
      reg_wb_ctrl_regwrite <= 1'h0; // @[stage-register.scala 52:9]
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
  reg [31:0] reg_instruction; // @[stage-register.scala 43:21]
  reg [31:0] reg_readdata; // @[stage-register.scala 43:21]
  reg [31:0] reg_ex_result; // @[stage-register.scala 43:21]
  assign io_data_instruction = reg_instruction; // @[stage-register.scala 45:11]
  assign io_data_readdata = reg_readdata; // @[stage-register.scala 45:11]
  assign io_data_ex_result = reg_ex_result; // @[stage-register.scala 45:11]
  always @(posedge clock) begin
    if (reset) begin // @[stage-register.scala 43:21]
      reg_instruction <= 32'h0; // @[stage-register.scala 43:21]
    end else begin
      reg_instruction <= io_in_instruction;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_readdata <= 32'h0; // @[stage-register.scala 43:21]
    end else begin
      reg_readdata <= io_in_readdata;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_ex_result <= 32'h0; // @[stage-register.scala 43:21]
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
  reg  reg_wb_ctrl_toreg; // @[stage-register.scala 43:21]
  reg  reg_wb_ctrl_regwrite; // @[stage-register.scala 43:21]
  assign io_data_wb_ctrl_toreg = reg_wb_ctrl_toreg; // @[stage-register.scala 45:11]
  assign io_data_wb_ctrl_regwrite = reg_wb_ctrl_regwrite; // @[stage-register.scala 45:11]
  always @(posedge clock) begin
    if (reset) begin // @[stage-register.scala 43:21]
      reg_wb_ctrl_toreg <= 1'h0; // @[stage-register.scala 43:21]
    end else begin
      reg_wb_ctrl_toreg <= io_in_wb_ctrl_toreg;
    end
    if (reset) begin // @[stage-register.scala 43:21]
      reg_wb_ctrl_regwrite <= 1'h0; // @[stage-register.scala 43:21]
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
module PipelinedCPUBP(
  input         clock,
  input         reset,
  output [31:0] io_imem_address,
  input  [31:0] io_imem_instruction,
  output [31:0] io_dmem_address,
  output        io_dmem_valid,
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
  wire [6:0] control_io_opcode; // @[cpu-bp.scala 93:31]
  wire  control_io_itype; // @[cpu-bp.scala 93:31]
  wire  control_io_aluop; // @[cpu-bp.scala 93:31]
  wire  control_io_xsrc; // @[cpu-bp.scala 93:31]
  wire  control_io_ysrc; // @[cpu-bp.scala 93:31]
  wire  control_io_branch; // @[cpu-bp.scala 93:31]
  wire  control_io_jal; // @[cpu-bp.scala 93:31]
  wire  control_io_jalr; // @[cpu-bp.scala 93:31]
  wire  control_io_plus4; // @[cpu-bp.scala 93:31]
  wire  control_io_resultselect; // @[cpu-bp.scala 93:31]
  wire [1:0] control_io_memop; // @[cpu-bp.scala 93:31]
  wire  control_io_toreg; // @[cpu-bp.scala 93:31]
  wire  control_io_regwrite; // @[cpu-bp.scala 93:31]
  wire  registers_clock; // @[cpu-bp.scala 94:31]
  wire [4:0] registers_io_readreg1; // @[cpu-bp.scala 94:31]
  wire [4:0] registers_io_readreg2; // @[cpu-bp.scala 94:31]
  wire [4:0] registers_io_writereg; // @[cpu-bp.scala 94:31]
  wire [31:0] registers_io_writedata; // @[cpu-bp.scala 94:31]
  wire  registers_io_wen; // @[cpu-bp.scala 94:31]
  wire [31:0] registers_io_readdata1; // @[cpu-bp.scala 94:31]
  wire [31:0] registers_io_readdata2; // @[cpu-bp.scala 94:31]
  wire  aluControl_io_aluop; // @[cpu-bp.scala 95:31]
  wire  aluControl_io_itype; // @[cpu-bp.scala 95:31]
  wire [6:0] aluControl_io_funct7; // @[cpu-bp.scala 95:31]
  wire [2:0] aluControl_io_funct3; // @[cpu-bp.scala 95:31]
  wire [3:0] aluControl_io_operation; // @[cpu-bp.scala 95:31]
  wire [3:0] alu_io_operation; // @[cpu-bp.scala 96:31]
  wire [31:0] alu_io_inputx; // @[cpu-bp.scala 96:31]
  wire [31:0] alu_io_inputy; // @[cpu-bp.scala 96:31]
  wire [31:0] alu_io_result; // @[cpu-bp.scala 96:31]
  wire [31:0] immGen_io_instruction; // @[cpu-bp.scala 97:31]
  wire [31:0] immGen_io_sextImm; // @[cpu-bp.scala 97:31]
  wire [31:0] pcPlusFour_io_inputx; // @[cpu-bp.scala 98:31]
  wire [31:0] pcPlusFour_io_inputy; // @[cpu-bp.scala 98:31]
  wire [31:0] pcPlusFour_io_result; // @[cpu-bp.scala 98:31]
  wire  nextPCmod_io_branch; // @[cpu-bp.scala 99:31]
  wire  nextPCmod_io_jal; // @[cpu-bp.scala 99:31]
  wire  nextPCmod_io_jalr; // @[cpu-bp.scala 99:31]
  wire [31:0] nextPCmod_io_inputx; // @[cpu-bp.scala 99:31]
  wire [31:0] nextPCmod_io_inputy; // @[cpu-bp.scala 99:31]
  wire [2:0] nextPCmod_io_funct3; // @[cpu-bp.scala 99:31]
  wire [31:0] nextPCmod_io_pc; // @[cpu-bp.scala 99:31]
  wire [31:0] nextPCmod_io_imm; // @[cpu-bp.scala 99:31]
  wire [31:0] nextPCmod_io_nextpc; // @[cpu-bp.scala 99:31]
  wire  nextPCmod_io_taken; // @[cpu-bp.scala 99:31]
  wire [4:0] forwarding_io_rs1; // @[cpu-bp.scala 100:31]
  wire [4:0] forwarding_io_rs2; // @[cpu-bp.scala 100:31]
  wire [4:0] forwarding_io_exmemrd; // @[cpu-bp.scala 100:31]
  wire  forwarding_io_exmemrw; // @[cpu-bp.scala 100:31]
  wire [4:0] forwarding_io_memwbrd; // @[cpu-bp.scala 100:31]
  wire  forwarding_io_memwbrw; // @[cpu-bp.scala 100:31]
  wire [1:0] forwarding_io_forwardA; // @[cpu-bp.scala 100:31]
  wire [1:0] forwarding_io_forwardB; // @[cpu-bp.scala 100:31]
  wire [4:0] hazard_io_rs1; // @[cpu-bp.scala 101:31]
  wire [4:0] hazard_io_rs2; // @[cpu-bp.scala 101:31]
  wire  hazard_io_idex_memread; // @[cpu-bp.scala 101:31]
  wire [4:0] hazard_io_idex_rd; // @[cpu-bp.scala 101:31]
  wire  hazard_io_exmem_taken; // @[cpu-bp.scala 101:31]
  wire [1:0] hazard_io_pcSel; // @[cpu-bp.scala 101:31]
  wire  hazard_io_if_id_stall; // @[cpu-bp.scala 101:31]
  wire  hazard_io_if_id_flush; // @[cpu-bp.scala 101:31]
  wire  hazard_io_id_ex_flush; // @[cpu-bp.scala 101:31]
  wire  hazard_io_ex_mem_flush; // @[cpu-bp.scala 101:31]
  wire [31:0] branchAdd_io_inputx; // @[cpu-bp.scala 103:31]
  wire [31:0] branchAdd_io_inputy; // @[cpu-bp.scala 103:31]
  wire [31:0] branchAdd_io_result; // @[cpu-bp.scala 103:31]
  wire  if_id_clock; // @[cpu-bp.scala 107:27]
  wire  if_id_reset; // @[cpu-bp.scala 107:27]
  wire [31:0] if_id_io_in_instruction; // @[cpu-bp.scala 107:27]
  wire [31:0] if_id_io_in_pc; // @[cpu-bp.scala 107:27]
  wire  if_id_io_flush; // @[cpu-bp.scala 107:27]
  wire  if_id_io_valid; // @[cpu-bp.scala 107:27]
  wire [31:0] if_id_io_data_instruction; // @[cpu-bp.scala 107:27]
  wire [31:0] if_id_io_data_pc; // @[cpu-bp.scala 107:27]
  wire  id_ex_clock; // @[cpu-bp.scala 109:27]
  wire  id_ex_reset; // @[cpu-bp.scala 109:27]
  wire [31:0] id_ex_io_in_pc; // @[cpu-bp.scala 109:27]
  wire [31:0] id_ex_io_in_instruction; // @[cpu-bp.scala 109:27]
  wire [31:0] id_ex_io_in_sextImm; // @[cpu-bp.scala 109:27]
  wire [31:0] id_ex_io_in_readdata1; // @[cpu-bp.scala 109:27]
  wire [31:0] id_ex_io_in_readdata2; // @[cpu-bp.scala 109:27]
  wire  id_ex_io_flush; // @[cpu-bp.scala 109:27]
  wire [31:0] id_ex_io_data_pc; // @[cpu-bp.scala 109:27]
  wire [31:0] id_ex_io_data_instruction; // @[cpu-bp.scala 109:27]
  wire [31:0] id_ex_io_data_sextImm; // @[cpu-bp.scala 109:27]
  wire [31:0] id_ex_io_data_readdata1; // @[cpu-bp.scala 109:27]
  wire [31:0] id_ex_io_data_readdata2; // @[cpu-bp.scala 109:27]
  wire  id_ex_ctrl_clock; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_reset; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_in_ex_ctrl_itype; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_in_ex_ctrl_aluop; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_in_ex_ctrl_resultselect; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_in_ex_ctrl_xsrc; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_in_ex_ctrl_ysrc; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_in_ex_ctrl_plus4; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_in_ex_ctrl_branch; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_in_ex_ctrl_jal; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_in_ex_ctrl_jalr; // @[cpu-bp.scala 110:27]
  wire [1:0] id_ex_ctrl_io_in_mem_ctrl_memop; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_in_wb_ctrl_toreg; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_in_wb_ctrl_regwrite; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_flush; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_data_ex_ctrl_itype; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_data_ex_ctrl_aluop; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_data_ex_ctrl_resultselect; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_data_ex_ctrl_xsrc; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_data_ex_ctrl_ysrc; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_data_ex_ctrl_plus4; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_data_ex_ctrl_branch; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_data_ex_ctrl_jal; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_data_ex_ctrl_jalr; // @[cpu-bp.scala 110:27]
  wire [1:0] id_ex_ctrl_io_data_mem_ctrl_memop; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_data_wb_ctrl_toreg; // @[cpu-bp.scala 110:27]
  wire  id_ex_ctrl_io_data_wb_ctrl_regwrite; // @[cpu-bp.scala 110:27]
  wire  ex_mem_clock; // @[cpu-bp.scala 112:27]
  wire  ex_mem_reset; // @[cpu-bp.scala 112:27]
  wire [31:0] ex_mem_io_in_ex_result; // @[cpu-bp.scala 112:27]
  wire [31:0] ex_mem_io_in_mem_writedata; // @[cpu-bp.scala 112:27]
  wire [31:0] ex_mem_io_in_instruction; // @[cpu-bp.scala 112:27]
  wire [31:0] ex_mem_io_in_next_pc; // @[cpu-bp.scala 112:27]
  wire  ex_mem_io_in_taken; // @[cpu-bp.scala 112:27]
  wire  ex_mem_io_flush; // @[cpu-bp.scala 112:27]
  wire [31:0] ex_mem_io_data_ex_result; // @[cpu-bp.scala 112:27]
  wire [31:0] ex_mem_io_data_mem_writedata; // @[cpu-bp.scala 112:27]
  wire [31:0] ex_mem_io_data_instruction; // @[cpu-bp.scala 112:27]
  wire [31:0] ex_mem_io_data_next_pc; // @[cpu-bp.scala 112:27]
  wire  ex_mem_io_data_taken; // @[cpu-bp.scala 112:27]
  wire  ex_mem_ctrl_clock; // @[cpu-bp.scala 113:27]
  wire  ex_mem_ctrl_reset; // @[cpu-bp.scala 113:27]
  wire [1:0] ex_mem_ctrl_io_in_mem_ctrl_memop; // @[cpu-bp.scala 113:27]
  wire  ex_mem_ctrl_io_in_wb_ctrl_toreg; // @[cpu-bp.scala 113:27]
  wire  ex_mem_ctrl_io_in_wb_ctrl_regwrite; // @[cpu-bp.scala 113:27]
  wire  ex_mem_ctrl_io_flush; // @[cpu-bp.scala 113:27]
  wire [1:0] ex_mem_ctrl_io_data_mem_ctrl_memop; // @[cpu-bp.scala 113:27]
  wire  ex_mem_ctrl_io_data_wb_ctrl_toreg; // @[cpu-bp.scala 113:27]
  wire  ex_mem_ctrl_io_data_wb_ctrl_regwrite; // @[cpu-bp.scala 113:27]
  wire  mem_wb_clock; // @[cpu-bp.scala 115:27]
  wire  mem_wb_reset; // @[cpu-bp.scala 115:27]
  wire [31:0] mem_wb_io_in_instruction; // @[cpu-bp.scala 115:27]
  wire [31:0] mem_wb_io_in_readdata; // @[cpu-bp.scala 115:27]
  wire [31:0] mem_wb_io_in_ex_result; // @[cpu-bp.scala 115:27]
  wire [31:0] mem_wb_io_data_instruction; // @[cpu-bp.scala 115:27]
  wire [31:0] mem_wb_io_data_readdata; // @[cpu-bp.scala 115:27]
  wire [31:0] mem_wb_io_data_ex_result; // @[cpu-bp.scala 115:27]
  wire  mem_wb_ctrl_clock; // @[cpu-bp.scala 119:27]
  wire  mem_wb_ctrl_reset; // @[cpu-bp.scala 119:27]
  wire  mem_wb_ctrl_io_in_wb_ctrl_toreg; // @[cpu-bp.scala 119:27]
  wire  mem_wb_ctrl_io_in_wb_ctrl_regwrite; // @[cpu-bp.scala 119:27]
  wire  mem_wb_ctrl_io_data_wb_ctrl_toreg; // @[cpu-bp.scala 119:27]
  wire  mem_wb_ctrl_io_data_wb_ctrl_regwrite; // @[cpu-bp.scala 119:27]
  reg [31:0] pc; // @[cpu-bp.scala 92:32]
  reg [31:0] bpCorrect; // @[cpu-bp.scala 121:28]
  reg [31:0] bpIncorrect; // @[cpu-bp.scala 122:28]
  wire  _T_5 = bpCorrect > 32'h100000; // @[cpu-bp.scala 123:19]
  wire  _T_8 = hazard_io_pcSel == 2'h0; // @[cpu-bp.scala 159:30]
  wire  _T_9 = hazard_io_pcSel == 2'h1; // @[cpu-bp.scala 160:30]
  wire  _T_10 = hazard_io_pcSel == 2'h2; // @[cpu-bp.scala 161:30]
  wire  _T_11 = hazard_io_pcSel == 2'h3; // @[cpu-bp.scala 162:30]
  wire [31:0] _T_12 = _T_11 ? pc : 32'h0; // @[Mux.scala 87:16]
  wire [31:0] id_next_pc = branchAdd_io_result; // @[cpu-bp.scala 149:24 206:14]
  wire [31:0] next_pc = ex_mem_io_data_next_pc; // @[cpu-bp.scala 148:24 400:11]
  wire [31:0] write_data = mem_wb_ctrl_io_data_wb_ctrl_toreg ? mem_wb_io_data_readdata : mem_wb_io_data_ex_result; // @[cpu-bp.scala 434:55 435:16 437:16]
  wire [31:0] _GEN_3 = forwarding_io_forwardA == 2'h1 ? ex_mem_io_data_ex_result : write_data; // @[cpu-bp.scala 284:48 285:19 287:19]
  wire [31:0] forward_a_mux = forwarding_io_forwardA == 2'h0 ? id_ex_io_data_readdata1 : _GEN_3; // @[cpu-bp.scala 282:41 283:19]
  wire [31:0] _GEN_5 = forwarding_io_forwardB == 2'h1 ? ex_mem_io_data_ex_result : write_data; // @[cpu-bp.scala 295:48 296:19 298:19]
  wire [31:0] forward_b_mux = forwarding_io_forwardB == 2'h0 ? id_ex_io_data_readdata2 : _GEN_5; // @[cpu-bp.scala 293:41 294:19]
  wire [31:0] _GEN_8 = id_ex_ctrl_io_data_ex_ctrl_ysrc ? id_ex_io_data_sextImm : forward_b_mux; // @[cpu-bp.scala 315:55 316:21 318:21]
  wire [31:0] _T_39 = bpCorrect + 32'h1; // @[cpu-bp.scala 356:32]
  wire [31:0] _T_41 = bpIncorrect + 32'h1; // @[cpu-bp.scala 359:34]
  wire  _GEN_14 = nextPCmod_io_taken; // @[cpu-bp.scala 349:71 352:25]
  Control control ( // @[cpu-bp.scala 93:31]
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
  RegisterFile registers ( // @[cpu-bp.scala 94:31]
    .clock(registers_clock),
    .io_readreg1(registers_io_readreg1),
    .io_readreg2(registers_io_readreg2),
    .io_writereg(registers_io_writereg),
    .io_writedata(registers_io_writedata),
    .io_wen(registers_io_wen),
    .io_readdata1(registers_io_readdata1),
    .io_readdata2(registers_io_readdata2)
  );
  ALUControl aluControl ( // @[cpu-bp.scala 95:31]
    .io_aluop(aluControl_io_aluop),
    .io_itype(aluControl_io_itype),
    .io_funct7(aluControl_io_funct7),
    .io_funct3(aluControl_io_funct3),
    .io_operation(aluControl_io_operation)
  );
  ALU alu ( // @[cpu-bp.scala 96:31]
    .io_operation(alu_io_operation),
    .io_inputx(alu_io_inputx),
    .io_inputy(alu_io_inputy),
    .io_result(alu_io_result)
  );
  ImmediateGenerator immGen ( // @[cpu-bp.scala 97:31]
    .io_instruction(immGen_io_instruction),
    .io_sextImm(immGen_io_sextImm)
  );
  Adder pcPlusFour ( // @[cpu-bp.scala 98:31]
    .io_inputx(pcPlusFour_io_inputx),
    .io_inputy(pcPlusFour_io_inputy),
    .io_result(pcPlusFour_io_result)
  );
  NextPC nextPCmod ( // @[cpu-bp.scala 99:31]
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
  ForwardingUnit forwarding ( // @[cpu-bp.scala 100:31]
    .io_rs1(forwarding_io_rs1),
    .io_rs2(forwarding_io_rs2),
    .io_exmemrd(forwarding_io_exmemrd),
    .io_exmemrw(forwarding_io_exmemrw),
    .io_memwbrd(forwarding_io_memwbrd),
    .io_memwbrw(forwarding_io_memwbrw),
    .io_forwardA(forwarding_io_forwardA),
    .io_forwardB(forwarding_io_forwardB)
  );
  HazardUnitBP hazard ( // @[cpu-bp.scala 101:31]
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
  Adder branchAdd ( // @[cpu-bp.scala 103:31]
    .io_inputx(branchAdd_io_inputx),
    .io_inputy(branchAdd_io_inputy),
    .io_result(branchAdd_io_result)
  );
  StageReg if_id ( // @[cpu-bp.scala 107:27]
    .clock(if_id_clock),
    .reset(if_id_reset),
    .io_in_instruction(if_id_io_in_instruction),
    .io_in_pc(if_id_io_in_pc),
    .io_flush(if_id_io_flush),
    .io_valid(if_id_io_valid),
    .io_data_instruction(if_id_io_data_instruction),
    .io_data_pc(if_id_io_data_pc)
  );
  StageReg_1 id_ex ( // @[cpu-bp.scala 109:27]
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
  StageReg_2 id_ex_ctrl ( // @[cpu-bp.scala 110:27]
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
  StageReg_3 ex_mem ( // @[cpu-bp.scala 112:27]
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
  StageReg_4 ex_mem_ctrl ( // @[cpu-bp.scala 113:27]
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
  StageReg_5 mem_wb ( // @[cpu-bp.scala 115:27]
    .clock(mem_wb_clock),
    .reset(mem_wb_reset),
    .io_in_instruction(mem_wb_io_in_instruction),
    .io_in_readdata(mem_wb_io_in_readdata),
    .io_in_ex_result(mem_wb_io_in_ex_result),
    .io_data_instruction(mem_wb_io_data_instruction),
    .io_data_readdata(mem_wb_io_data_readdata),
    .io_data_ex_result(mem_wb_io_data_ex_result)
  );
  StageReg_6 mem_wb_ctrl ( // @[cpu-bp.scala 119:27]
    .clock(mem_wb_ctrl_clock),
    .reset(mem_wb_ctrl_reset),
    .io_in_wb_ctrl_toreg(mem_wb_ctrl_io_in_wb_ctrl_toreg),
    .io_in_wb_ctrl_regwrite(mem_wb_ctrl_io_in_wb_ctrl_regwrite),
    .io_data_wb_ctrl_toreg(mem_wb_ctrl_io_data_wb_ctrl_toreg),
    .io_data_wb_ctrl_regwrite(mem_wb_ctrl_io_data_wb_ctrl_regwrite)
  );
  assign io_imem_address = pc; // @[cpu-bp.scala 164:19]
  assign io_dmem_address = ex_mem_io_data_ex_result; // @[cpu-bp.scala 391:21]
  assign io_dmem_valid = ex_mem_ctrl_io_data_mem_ctrl_memop[1]; // @[cpu-bp.scala 394:58]
  assign io_dmem_writedata = ex_mem_io_data_mem_writedata; // @[cpu-bp.scala 397:21]
  assign io_dmem_memread = ex_mem_ctrl_io_data_mem_ctrl_memop == 2'h2; // @[cpu-bp.scala 392:59]
  assign io_dmem_memwrite = ex_mem_ctrl_io_data_mem_ctrl_memop == 2'h3; // @[cpu-bp.scala 393:59]
  assign io_dmem_maskmode = ex_mem_io_data_instruction[13:12]; // @[cpu-bp.scala 395:50]
  assign io_dmem_sext = ~ex_mem_io_data_instruction[14]; // @[cpu-bp.scala 396:24]
  assign control_io_opcode = if_id_io_data_instruction[6:0]; // @[cpu-bp.scala 184:49]
  assign registers_clock = clock;
  assign registers_io_readreg1 = if_id_io_data_instruction[19:15]; // @[cpu-bp.scala 187:38]
  assign registers_io_readreg2 = if_id_io_data_instruction[24:20]; // @[cpu-bp.scala 188:38]
  assign registers_io_writereg = mem_wb_io_data_instruction[11:7]; // @[cpu-bp.scala 431:54]
  assign registers_io_writedata = mem_wb_ctrl_io_data_wb_ctrl_toreg ? mem_wb_io_data_readdata : mem_wb_io_data_ex_result
    ; // @[cpu-bp.scala 434:55 435:16 437:16]
  assign registers_io_wen = mem_wb_io_data_instruction[11:7] == 5'h0 ? 1'h0 : mem_wb_ctrl_io_data_wb_ctrl_regwrite; // @[cpu-bp.scala 425:51 426:22 428:22]
  assign aluControl_io_aluop = id_ex_ctrl_io_data_ex_ctrl_aluop; // @[cpu-bp.scala 270:24]
  assign aluControl_io_itype = id_ex_ctrl_io_data_ex_ctrl_itype; // @[cpu-bp.scala 269:24]
  assign aluControl_io_funct7 = id_ex_io_data_instruction[31:25]; // @[cpu-bp.scala 272:52]
  assign aluControl_io_funct3 = id_ex_io_data_instruction[14:12]; // @[cpu-bp.scala 271:52]
  assign alu_io_operation = aluControl_io_operation; // @[cpu-bp.scala 302:20]
  assign alu_io_inputx = id_ex_ctrl_io_data_ex_ctrl_xsrc ? id_ex_io_data_pc : forward_a_mux; // @[cpu-bp.scala 306:53 307:19 309:19]
  assign alu_io_inputy = id_ex_ctrl_io_data_ex_ctrl_plus4 ? 32'h4 : _GEN_8; // @[cpu-bp.scala 312:54 313:19]
  assign immGen_io_instruction = if_id_io_data_instruction; // @[cpu-bp.scala 200:25]
  assign pcPlusFour_io_inputx = pc; // @[cpu-bp.scala 168:24]
  assign pcPlusFour_io_inputy = 32'h4; // @[cpu-bp.scala 169:24]
  assign nextPCmod_io_branch = id_ex_ctrl_io_data_ex_ctrl_branch; // @[cpu-bp.scala 275:23]
  assign nextPCmod_io_jal = id_ex_ctrl_io_data_ex_ctrl_jal; // @[cpu-bp.scala 276:23]
  assign nextPCmod_io_jalr = id_ex_ctrl_io_data_ex_ctrl_jalr; // @[cpu-bp.scala 277:23]
  assign nextPCmod_io_inputx = forwarding_io_forwardA == 2'h0 ? id_ex_io_data_readdata1 : _GEN_3; // @[cpu-bp.scala 282:41 283:19]
  assign nextPCmod_io_inputy = forwarding_io_forwardB == 2'h0 ? id_ex_io_data_readdata2 : _GEN_5; // @[cpu-bp.scala 293:41 294:19]
  assign nextPCmod_io_funct3 = id_ex_io_data_instruction[14:12]; // @[cpu-bp.scala 325:51]
  assign nextPCmod_io_pc = id_ex_io_data_pc; // @[cpu-bp.scala 326:23]
  assign nextPCmod_io_imm = id_ex_io_data_sextImm; // @[cpu-bp.scala 327:23]
  assign forwarding_io_rs1 = id_ex_io_data_instruction[19:15]; // @[cpu-bp.scala 263:53]
  assign forwarding_io_rs2 = id_ex_io_data_instruction[24:20]; // @[cpu-bp.scala 264:53]
  assign forwarding_io_exmemrd = ex_mem_io_data_instruction[11:7]; // @[cpu-bp.scala 265:54]
  assign forwarding_io_exmemrw = ex_mem_ctrl_io_data_wb_ctrl_regwrite; // @[cpu-bp.scala 266:25]
  assign forwarding_io_memwbrd = mem_wb_io_data_instruction[11:7]; // @[cpu-bp.scala 441:54]
  assign forwarding_io_memwbrw = mem_wb_ctrl_io_data_wb_ctrl_regwrite; // @[cpu-bp.scala 442:25]
  assign hazard_io_rs1 = if_id_io_data_instruction[19:15]; // @[cpu-bp.scala 187:38]
  assign hazard_io_rs2 = if_id_io_data_instruction[24:20]; // @[cpu-bp.scala 188:38]
  assign hazard_io_idex_memread = id_ex_ctrl_io_data_mem_ctrl_memop == 2'h2; // @[cpu-bp.scala 256:43]
  assign hazard_io_idex_rd = id_ex_io_data_instruction[11:7]; // @[cpu-bp.scala 254:49]
  assign hazard_io_exmem_taken = ex_mem_io_data_taken; // @[cpu-bp.scala 403:25]
  assign branchAdd_io_inputx = if_id_io_data_pc; // @[cpu-bp.scala 203:23]
  assign branchAdd_io_inputy = immGen_io_sextImm; // @[cpu-bp.scala 204:23]
  assign if_id_clock = clock;
  assign if_id_reset = reset;
  assign if_id_io_in_instruction = io_imem_instruction; // @[cpu-bp.scala 172:27]
  assign if_id_io_in_pc = pc; // @[cpu-bp.scala 173:27]
  assign if_id_io_flush = hazard_io_if_id_flush; // @[cpu-bp.scala 177:18]
  assign if_id_io_valid = ~hazard_io_if_id_stall; // @[cpu-bp.scala 176:21]
  assign id_ex_clock = clock;
  assign id_ex_reset = reset;
  assign id_ex_io_in_pc = if_id_io_data_pc; // @[cpu-bp.scala 218:27]
  assign id_ex_io_in_instruction = if_id_io_data_instruction; // @[cpu-bp.scala 220:27]
  assign id_ex_io_in_sextImm = immGen_io_sextImm; // @[cpu-bp.scala 219:27]
  assign id_ex_io_in_readdata1 = registers_io_readdata1; // @[cpu-bp.scala 221:27]
  assign id_ex_io_in_readdata2 = registers_io_readdata2; // @[cpu-bp.scala 222:27]
  assign id_ex_io_flush = hazard_io_id_ex_flush; // @[cpu-bp.scala 244:18]
  assign id_ex_ctrl_clock = clock;
  assign id_ex_ctrl_reset = reset;
  assign id_ex_ctrl_io_in_ex_ctrl_itype = control_io_itype; // @[cpu-bp.scala 226:41]
  assign id_ex_ctrl_io_in_ex_ctrl_aluop = control_io_aluop; // @[cpu-bp.scala 225:41]
  assign id_ex_ctrl_io_in_ex_ctrl_resultselect = control_io_resultselect; // @[cpu-bp.scala 227:41]
  assign id_ex_ctrl_io_in_ex_ctrl_xsrc = control_io_xsrc; // @[cpu-bp.scala 228:41]
  assign id_ex_ctrl_io_in_ex_ctrl_ysrc = control_io_ysrc; // @[cpu-bp.scala 229:41]
  assign id_ex_ctrl_io_in_ex_ctrl_plus4 = control_io_plus4; // @[cpu-bp.scala 230:41]
  assign id_ex_ctrl_io_in_ex_ctrl_branch = control_io_branch; // @[cpu-bp.scala 231:41]
  assign id_ex_ctrl_io_in_ex_ctrl_jal = control_io_jal; // @[cpu-bp.scala 232:41]
  assign id_ex_ctrl_io_in_ex_ctrl_jalr = control_io_jalr; // @[cpu-bp.scala 233:41]
  assign id_ex_ctrl_io_in_mem_ctrl_memop = control_io_memop; // @[cpu-bp.scala 237:35]
  assign id_ex_ctrl_io_in_wb_ctrl_toreg = control_io_toreg; // @[cpu-bp.scala 241:37]
  assign id_ex_ctrl_io_in_wb_ctrl_regwrite = control_io_regwrite; // @[cpu-bp.scala 240:37]
  assign id_ex_ctrl_io_flush = hazard_io_id_ex_flush; // @[cpu-bp.scala 247:23]
  assign ex_mem_clock = clock;
  assign ex_mem_reset = reset;
  assign ex_mem_io_in_ex_result = ~id_ex_ctrl_io_data_ex_ctrl_resultselect ? alu_io_result : id_ex_io_data_sextImm; // @[cpu-bp.scala 342:58 343:28 345:28]
  assign ex_mem_io_in_mem_writedata = forwarding_io_forwardB == 2'h0 ? id_ex_io_data_readdata2 : _GEN_5; // @[cpu-bp.scala 293:41 294:19]
  assign ex_mem_io_in_instruction = id_ex_io_data_instruction; // @[cpu-bp.scala 330:30]
  assign ex_mem_io_in_next_pc = nextPCmod_io_nextpc; // @[cpu-bp.scala 338:24]
  assign ex_mem_io_in_taken = id_ex_ctrl_io_data_ex_ctrl_branch ? _GEN_14 : nextPCmod_io_taken; // @[cpu-bp.scala 339:24 371:43]
  assign ex_mem_io_flush = hazard_io_ex_mem_flush; // @[cpu-bp.scala 382:24]
  assign ex_mem_ctrl_clock = clock;
  assign ex_mem_ctrl_reset = reset;
  assign ex_mem_ctrl_io_in_mem_ctrl_memop = id_ex_ctrl_io_data_mem_ctrl_memop; // @[cpu-bp.scala 333:38]
  assign ex_mem_ctrl_io_in_wb_ctrl_toreg = id_ex_ctrl_io_data_wb_ctrl_toreg; // @[cpu-bp.scala 335:38]
  assign ex_mem_ctrl_io_in_wb_ctrl_regwrite = id_ex_ctrl_io_data_wb_ctrl_regwrite; // @[cpu-bp.scala 334:38]
  assign ex_mem_ctrl_io_flush = hazard_io_ex_mem_flush; // @[cpu-bp.scala 384:24]
  assign mem_wb_clock = clock;
  assign mem_wb_reset = reset;
  assign mem_wb_io_in_instruction = ex_mem_io_data_instruction; // @[cpu-bp.scala 409:28]
  assign mem_wb_io_in_readdata = io_dmem_readdata; // @[cpu-bp.scala 410:28]
  assign mem_wb_io_in_ex_result = ex_mem_io_data_ex_result; // @[cpu-bp.scala 408:28]
  assign mem_wb_ctrl_clock = clock;
  assign mem_wb_ctrl_reset = reset;
  assign mem_wb_ctrl_io_in_wb_ctrl_toreg = ex_mem_ctrl_io_data_wb_ctrl_toreg; // @[cpu-bp.scala 413:38]
  assign mem_wb_ctrl_io_in_wb_ctrl_regwrite = ex_mem_ctrl_io_data_wb_ctrl_regwrite; // @[cpu-bp.scala 412:38]
  always @(posedge clock) begin
    if (reset) begin // @[cpu-bp.scala 92:32]
      pc <= 32'h0; // @[cpu-bp.scala 92:32]
    end else if (_T_8) begin // @[Mux.scala 87:16]
      pc <= pcPlusFour_io_result;
    end else if (_T_9) begin // @[Mux.scala 87:16]
      pc <= next_pc;
    end else if (_T_10) begin // @[Mux.scala 87:16]
      pc <= id_next_pc;
    end else begin
      pc <= _T_12;
    end
    if (reset) begin // @[cpu-bp.scala 121:28]
      bpCorrect <= 32'h0; // @[cpu-bp.scala 121:28]
    end else if (id_ex_ctrl_io_data_ex_ctrl_branch & ~hazard_io_ex_mem_flush) begin // @[cpu-bp.scala 349:71]
      if (~nextPCmod_io_taken) begin // @[cpu-bp.scala 355:73]
        bpCorrect <= _T_39; // @[cpu-bp.scala 356:19]
      end
    end
    if (reset) begin // @[cpu-bp.scala 122:28]
      bpIncorrect <= 32'h0; // @[cpu-bp.scala 122:28]
    end else if (id_ex_ctrl_io_data_ex_ctrl_branch & ~hazard_io_ex_mem_flush) begin // @[cpu-bp.scala 349:71]
      if (!(~nextPCmod_io_taken)) begin // @[cpu-bp.scala 355:73]
        bpIncorrect <= _T_41; // @[cpu-bp.scala 359:19]
      end
    end
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_5 & ~reset) begin
          $fwrite(32'h80000002,"BP correct: %d; incorrect: %d\n",bpCorrect,bpIncorrect); // @[cpu-bp.scala 125:11]
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
module DualPortedCombinMemory(
  input         clock,
  input         reset,
  input  [31:0] io_imem_request_bits_address,
  output [31:0] io_imem_response_bits_data,
  input         io_dmem_request_valid,
  input  [31:0] io_dmem_request_bits_address,
  input  [31:0] io_dmem_request_bits_writedata,
  input  [1:0]  io_dmem_request_bits_operation,
  output        io_dmem_response_valid,
  output [31:0] io_dmem_response_bits_data
);
`ifdef RANDOMIZE_MEM_INIT
  reg [31:0] _RAND_0;
`endif // RANDOMIZE_MEM_INIT
  reg [31:0] memory [0:16383]; // @[base-memory-components.scala 39:19]
  wire  memory__T_9_en; // @[base-memory-components.scala 39:19]
  wire [13:0] memory__T_9_addr; // @[base-memory-components.scala 39:19]
  wire [31:0] memory__T_9_data; // @[base-memory-components.scala 39:19]
  wire  memory__T_20_en; // @[base-memory-components.scala 39:19]
  wire [13:0] memory__T_20_addr; // @[base-memory-components.scala 39:19]
  wire [31:0] memory__T_20_data; // @[base-memory-components.scala 39:19]
  wire [31:0] memory__T_24_data; // @[base-memory-components.scala 39:19]
  wire [13:0] memory__T_24_addr; // @[base-memory-components.scala 39:19]
  wire  memory__T_24_mask; // @[base-memory-components.scala 39:19]
  wire  memory__T_24_en; // @[base-memory-components.scala 39:19]
  wire  _T_21 = io_dmem_request_bits_operation == 2'h2; // @[memory.scala 65:29]
  assign memory__T_9_en = io_imem_request_bits_address < 32'h10000;
  assign memory__T_9_addr = io_imem_request_bits_address[15:2];
  assign memory__T_9_data = memory[memory__T_9_addr]; // @[base-memory-components.scala 39:19]
  assign memory__T_20_en = io_dmem_request_valid;
  assign memory__T_20_addr = io_dmem_request_bits_address[15:2];
  assign memory__T_20_data = memory[memory__T_20_addr]; // @[base-memory-components.scala 39:19]
  assign memory__T_24_data = io_dmem_request_bits_writedata;
  assign memory__T_24_addr = io_dmem_request_bits_address[15:2];
  assign memory__T_24_mask = 1'h1;
  assign memory__T_24_en = io_dmem_request_valid & _T_21;
  assign io_imem_response_bits_data = io_imem_request_bits_address < 32'h10000 ? memory__T_9_data : 32'h0; // @[memory.scala 35:37 base-memory-components.scala 36:20 memory.scala 37:34]
  assign io_dmem_response_valid = io_dmem_request_valid; // @[base-memory-components.scala 39:19 memory.scala 52:32 61:46]
  assign io_dmem_response_bits_data = io_dmem_request_valid ? memory__T_20_data : 32'h0; // @[base-memory-components.scala 37:20 memory.scala 52:32 61:32]
  always @(posedge clock) begin
    if (memory__T_24_en & memory__T_24_mask) begin
      memory[memory__T_24_addr] <= memory__T_24_data; // @[base-memory-components.scala 39:19]
    end
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_dmem_request_valid & ~(io_dmem_request_bits_operation != 2'h1 | reset)) begin
          $fwrite(32'h80000002,"Assertion failed\n    at memory.scala:56 assert (request.operation =/= Write)\n"); // @[memory.scala 56:12]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_dmem_request_valid & ~(io_dmem_request_bits_operation != 2'h1 | reset)) begin
          $fatal; // @[memory.scala 56:12]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (io_dmem_request_valid & ~(io_dmem_request_bits_address < 32'h10000 | reset)) begin
          $fwrite(32'h80000002,"Assertion failed\n    at memory.scala:58 assert (request.address < size.U)\n"); // @[memory.scala 58:12]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (io_dmem_request_valid & ~(io_dmem_request_bits_address < 32'h10000 | reset)) begin
          $fatal; // @[memory.scala 58:12]
        end
    `ifdef STOP_COND
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
`ifdef RANDOMIZE_MEM_INIT
  _RAND_0 = {1{`RANDOM}};
  for (initvar = 0; initvar < 16384; initvar = initvar+1)
    memory[initvar] = _RAND_0[31:0];
`endif // RANDOMIZE_MEM_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
module ICombinMemPort(
  input  [31:0] io_pipeline_address,
  output [31:0] io_pipeline_instruction,
  output [31:0] io_bus_request_bits_address,
  input  [31:0] io_bus_response_bits_data
);
  assign io_pipeline_instruction = io_bus_response_bits_data; // @[memory-combin-ports.scala 33:27]
  assign io_bus_request_bits_address = io_pipeline_address; // @[memory-combin-ports.scala 17:23 18:23]
endmodule
module DCombinMemPort(
  input         clock,
  input         reset,
  input  [31:0] io_pipeline_address,
  input         io_pipeline_valid,
  input  [31:0] io_pipeline_writedata,
  input         io_pipeline_memread,
  input         io_pipeline_memwrite,
  input  [1:0]  io_pipeline_maskmode,
  input         io_pipeline_sext,
  output [31:0] io_pipeline_readdata,
  output        io_bus_request_valid,
  output [31:0] io_bus_request_bits_address,
  output [31:0] io_bus_request_bits_writedata,
  output [1:0]  io_bus_request_bits_operation,
  input         io_bus_response_valid,
  input  [31:0] io_bus_response_bits_data
);
  wire  _T_2 = io_pipeline_valid & (io_pipeline_memread | io_pipeline_memwrite); // @[memory-combin-ports.scala 44:27]
  wire  _T_12 = io_pipeline_maskmode == 2'h0; // @[memory-combin-ports.scala 85:36]
  wire  _T_13 = io_pipeline_address[1:0] == 2'h0; // @[memory-combin-ports.scala 86:23]
  wire [31:0] _T_16 = {io_bus_response_bits_data[31:8],io_pipeline_writedata[7:0]}; // @[Cat.scala 30:58]
  wire [31:0] _T_22 = {io_bus_response_bits_data[31:16],io_pipeline_writedata[15:8],io_bus_response_bits_data[7:0]}; // @[Cat.scala 30:58]
  wire [31:0] _T_28 = {io_bus_response_bits_data[31:24],io_pipeline_writedata[23:16],io_bus_response_bits_data[15:0]}; // @[Cat.scala 30:58]
  wire [31:0] _T_31 = {io_pipeline_writedata[31:24],io_bus_response_bits_data[23:0]}; // @[Cat.scala 30:58]
  wire [31:0] _GEN_4 = io_pipeline_address[1:0] == 2'h2 ? _T_28 : _T_31; // @[memory-combin-ports.scala 90:38 91:23 93:23]
  wire [31:0] _GEN_5 = io_pipeline_address[1:0] == 2'h1 ? _T_22 : _GEN_4; // @[memory-combin-ports.scala 88:38 89:23]
  wire [31:0] _GEN_6 = io_pipeline_address[1:0] == 2'h0 ? _T_16 : _GEN_5; // @[memory-combin-ports.scala 86:32 87:23]
  wire [31:0] _T_35 = {io_bus_response_bits_data[31:16],io_pipeline_writedata[15:0]}; // @[Cat.scala 30:58]
  wire [31:0] _T_38 = {io_pipeline_writedata[31:16],io_bus_response_bits_data[15:0]}; // @[Cat.scala 30:58]
  wire [31:0] _GEN_7 = _T_13 ? _T_35 : _T_38; // @[memory-combin-ports.scala 96:33 97:23 99:23]
  wire [31:0] _GEN_8 = io_pipeline_maskmode == 2'h0 ? _GEN_6 : _GEN_7; // @[memory-combin-ports.scala 85:45]
  wire [5:0] _T_43 = io_pipeline_address[1:0] * 4'h8; // @[memory-combin-ports.scala 116:64]
  wire [31:0] _T_44 = io_bus_response_bits_data >> _T_43; // @[memory-combin-ports.scala 116:53]
  wire [31:0] _T_45 = _T_44 & 32'hff; // @[memory-combin-ports.scala 116:72]
  wire  _T_46 = io_pipeline_maskmode == 2'h1; // @[memory-combin-ports.scala 117:41]
  wire [31:0] _T_49 = _T_44 & 32'hffff; // @[memory-combin-ports.scala 119:72]
  wire [31:0] _GEN_10 = io_pipeline_maskmode == 2'h1 ? _T_49 : io_bus_response_bits_data; // @[memory-combin-ports.scala 117:50 119:23 121:23]
  wire [31:0] _GEN_11 = _T_12 ? _T_45 : _GEN_10; // @[memory-combin-ports.scala 114:43 116:23]
  wire [23:0] _T_53 = _GEN_11[7] ? 24'hffffff : 24'h0; // @[Bitwise.scala 72:12]
  wire [31:0] _T_55 = {_T_53,_GEN_11[7:0]}; // @[Cat.scala 30:58]
  wire [15:0] _T_59 = _GEN_11[15] ? 16'hffff : 16'h0; // @[Bitwise.scala 72:12]
  wire [31:0] _T_61 = {_T_59,_GEN_11[15:0]}; // @[Cat.scala 30:58]
  wire [31:0] _GEN_12 = _T_46 ? _T_61 : _GEN_11; // @[memory-combin-ports.scala 128:52 130:30 133:30]
  wire [31:0] _GEN_13 = _T_12 ? _T_55 : _GEN_12; // @[memory-combin-ports.scala 125:45 127:30]
  wire [31:0] _GEN_14 = io_pipeline_sext ? _GEN_13 : _GEN_11; // @[memory-combin-ports.scala 124:31 136:28]
  wire [31:0] _GEN_15 = io_pipeline_memread ? _GEN_14 : 32'h0; // @[memory-combin-ports.scala 108:39 139:28 base-memory-components.scala 69:15]
  wire [31:0] _GEN_17 = io_pipeline_memwrite ? 32'h0 : _GEN_15; // @[base-memory-components.scala 69:15 memory-combin-ports.scala 72:33]
  assign io_pipeline_readdata = io_bus_response_valid ? _GEN_17 : 32'h0; // @[base-memory-components.scala 69:15 memory-combin-ports.scala 71:32]
  assign io_bus_request_valid = io_pipeline_valid & (io_pipeline_memread | io_pipeline_memwrite); // @[memory-combin-ports.scala 44:27]
  assign io_bus_request_bits_address = io_pipeline_address; // @[memory-combin-ports.scala 44:77 48:33]
  assign io_bus_request_bits_writedata = io_pipeline_maskmode != 2'h2 ? _GEN_8 : io_pipeline_writedata; // @[memory-combin-ports.scala 104:19 77:43]
  assign io_bus_request_bits_operation = io_pipeline_memwrite ? 2'h2 : 2'h0; // @[memory-combin-ports.scala 51:33 60:37 63:37]
  always @(posedge clock) begin
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_2 & ~(~(io_pipeline_memread & io_pipeline_memwrite) | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed\n    at memory-combin-ports.scala:46 assert(!(io.pipeline.memread && io.pipeline.memwrite))\n"
            ); // @[memory-combin-ports.scala 46:11]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_2 & ~(~(io_pipeline_memread & io_pipeline_memwrite) | reset)) begin
          $fatal; // @[memory-combin-ports.scala 46:11]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
  end
endmodule
module Dino1Core(
  input   clock,
  input   reset,
  output  io_success
);
  wire  cpu0_clock; // @[top.scala 12:20]
  wire  cpu0_reset; // @[top.scala 12:20]
  wire [31:0] cpu0_io_imem_address; // @[top.scala 12:20]
  wire [31:0] cpu0_io_imem_instruction; // @[top.scala 12:20]
  wire [31:0] cpu0_io_dmem_address; // @[top.scala 12:20]
  wire  cpu0_io_dmem_valid; // @[top.scala 12:20]
  wire [31:0] cpu0_io_dmem_writedata; // @[top.scala 12:20]
  wire  cpu0_io_dmem_memread; // @[top.scala 12:20]
  wire  cpu0_io_dmem_memwrite; // @[top.scala 12:20]
  wire [1:0] cpu0_io_dmem_maskmode; // @[top.scala 12:20]
  wire  cpu0_io_dmem_sext; // @[top.scala 12:20]
  wire [31:0] cpu0_io_dmem_readdata; // @[top.scala 12:20]
  wire  mem0_clock; // @[top.scala 13:20]
  wire  mem0_reset; // @[top.scala 13:20]
  wire [31:0] mem0_io_imem_request_bits_address; // @[top.scala 13:20]
  wire [31:0] mem0_io_imem_response_bits_data; // @[top.scala 13:20]
  wire  mem0_io_dmem_request_valid; // @[top.scala 13:20]
  wire [31:0] mem0_io_dmem_request_bits_address; // @[top.scala 13:20]
  wire [31:0] mem0_io_dmem_request_bits_writedata; // @[top.scala 13:20]
  wire [1:0] mem0_io_dmem_request_bits_operation; // @[top.scala 13:20]
  wire  mem0_io_dmem_response_valid; // @[top.scala 13:20]
  wire [31:0] mem0_io_dmem_response_bits_data; // @[top.scala 13:20]
  wire [31:0] imem0_io_pipeline_address; // @[top.scala 14:21]
  wire [31:0] imem0_io_pipeline_instruction; // @[top.scala 14:21]
  wire [31:0] imem0_io_bus_request_bits_address; // @[top.scala 14:21]
  wire [31:0] imem0_io_bus_response_bits_data; // @[top.scala 14:21]
  wire  dmem0_clock; // @[top.scala 15:21]
  wire  dmem0_reset; // @[top.scala 15:21]
  wire [31:0] dmem0_io_pipeline_address; // @[top.scala 15:21]
  wire  dmem0_io_pipeline_valid; // @[top.scala 15:21]
  wire [31:0] dmem0_io_pipeline_writedata; // @[top.scala 15:21]
  wire  dmem0_io_pipeline_memread; // @[top.scala 15:21]
  wire  dmem0_io_pipeline_memwrite; // @[top.scala 15:21]
  wire [1:0] dmem0_io_pipeline_maskmode; // @[top.scala 15:21]
  wire  dmem0_io_pipeline_sext; // @[top.scala 15:21]
  wire [31:0] dmem0_io_pipeline_readdata; // @[top.scala 15:21]
  wire  dmem0_io_bus_request_valid; // @[top.scala 15:21]
  wire [31:0] dmem0_io_bus_request_bits_address; // @[top.scala 15:21]
  wire [31:0] dmem0_io_bus_request_bits_writedata; // @[top.scala 15:21]
  wire [1:0] dmem0_io_bus_request_bits_operation; // @[top.scala 15:21]
  wire  dmem0_io_bus_response_valid; // @[top.scala 15:21]
  wire [31:0] dmem0_io_bus_response_bits_data; // @[top.scala 15:21]
  PipelinedCPUBP cpu0 ( // @[top.scala 12:20]
    .clock(cpu0_clock),
    .reset(cpu0_reset),
    .io_imem_address(cpu0_io_imem_address),
    .io_imem_instruction(cpu0_io_imem_instruction),
    .io_dmem_address(cpu0_io_dmem_address),
    .io_dmem_valid(cpu0_io_dmem_valid),
    .io_dmem_writedata(cpu0_io_dmem_writedata),
    .io_dmem_memread(cpu0_io_dmem_memread),
    .io_dmem_memwrite(cpu0_io_dmem_memwrite),
    .io_dmem_maskmode(cpu0_io_dmem_maskmode),
    .io_dmem_sext(cpu0_io_dmem_sext),
    .io_dmem_readdata(cpu0_io_dmem_readdata)
  );
  DualPortedCombinMemory mem0 ( // @[top.scala 13:20]
    .clock(mem0_clock),
    .reset(mem0_reset),
    .io_imem_request_bits_address(mem0_io_imem_request_bits_address),
    .io_imem_response_bits_data(mem0_io_imem_response_bits_data),
    .io_dmem_request_valid(mem0_io_dmem_request_valid),
    .io_dmem_request_bits_address(mem0_io_dmem_request_bits_address),
    .io_dmem_request_bits_writedata(mem0_io_dmem_request_bits_writedata),
    .io_dmem_request_bits_operation(mem0_io_dmem_request_bits_operation),
    .io_dmem_response_valid(mem0_io_dmem_response_valid),
    .io_dmem_response_bits_data(mem0_io_dmem_response_bits_data)
  );
  ICombinMemPort imem0 ( // @[top.scala 14:21]
    .io_pipeline_address(imem0_io_pipeline_address),
    .io_pipeline_instruction(imem0_io_pipeline_instruction),
    .io_bus_request_bits_address(imem0_io_bus_request_bits_address),
    .io_bus_response_bits_data(imem0_io_bus_response_bits_data)
  );
  DCombinMemPort dmem0 ( // @[top.scala 15:21]
    .clock(dmem0_clock),
    .reset(dmem0_reset),
    .io_pipeline_address(dmem0_io_pipeline_address),
    .io_pipeline_valid(dmem0_io_pipeline_valid),
    .io_pipeline_writedata(dmem0_io_pipeline_writedata),
    .io_pipeline_memread(dmem0_io_pipeline_memread),
    .io_pipeline_memwrite(dmem0_io_pipeline_memwrite),
    .io_pipeline_maskmode(dmem0_io_pipeline_maskmode),
    .io_pipeline_sext(dmem0_io_pipeline_sext),
    .io_pipeline_readdata(dmem0_io_pipeline_readdata),
    .io_bus_request_valid(dmem0_io_bus_request_valid),
    .io_bus_request_bits_address(dmem0_io_bus_request_bits_address),
    .io_bus_request_bits_writedata(dmem0_io_bus_request_bits_writedata),
    .io_bus_request_bits_operation(dmem0_io_bus_request_bits_operation),
    .io_bus_response_valid(dmem0_io_bus_response_valid),
    .io_bus_response_bits_data(dmem0_io_bus_response_bits_data)
  );
  assign io_success = 1'h0;
  assign cpu0_clock = clock;
  assign cpu0_reset = reset;
  assign cpu0_io_imem_instruction = imem0_io_pipeline_instruction; // @[top.scala 17:16]
  assign cpu0_io_dmem_readdata = dmem0_io_pipeline_readdata; // @[top.scala 18:16]
  assign mem0_clock = clock;
  assign mem0_reset = reset;
  assign mem0_io_imem_request_bits_address = imem0_io_bus_request_bits_address; // @[base-memory-components.scala 16:26]
  assign mem0_io_dmem_request_valid = dmem0_io_bus_request_valid; // @[base-memory-components.scala 19:26]
  assign mem0_io_dmem_request_bits_address = dmem0_io_bus_request_bits_address; // @[base-memory-components.scala 19:26]
  assign mem0_io_dmem_request_bits_writedata = dmem0_io_bus_request_bits_writedata; // @[base-memory-components.scala 19:26]
  assign mem0_io_dmem_request_bits_operation = dmem0_io_bus_request_bits_operation; // @[base-memory-components.scala 19:26]
  assign imem0_io_pipeline_address = cpu0_io_imem_address; // @[top.scala 17:16]
  assign imem0_io_bus_response_bits_data = mem0_io_imem_response_bits_data; // @[base-memory-components.scala 17:26]
  assign dmem0_clock = clock;
  assign dmem0_reset = reset;
  assign dmem0_io_pipeline_address = cpu0_io_dmem_address; // @[top.scala 18:16]
  assign dmem0_io_pipeline_valid = cpu0_io_dmem_valid; // @[top.scala 18:16]
  assign dmem0_io_pipeline_writedata = cpu0_io_dmem_writedata; // @[top.scala 18:16]
  assign dmem0_io_pipeline_memread = cpu0_io_dmem_memread; // @[top.scala 18:16]
  assign dmem0_io_pipeline_memwrite = cpu0_io_dmem_memwrite; // @[top.scala 18:16]
  assign dmem0_io_pipeline_maskmode = cpu0_io_dmem_maskmode; // @[top.scala 18:16]
  assign dmem0_io_pipeline_sext = cpu0_io_dmem_sext; // @[top.scala 18:16]
  assign dmem0_io_bus_response_valid = mem0_io_dmem_response_valid; // @[base-memory-components.scala 20:26]
  assign dmem0_io_bus_response_bits_data = mem0_io_dmem_response_bits_data; // @[base-memory-components.scala 20:26]
endmodule
