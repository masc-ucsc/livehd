module Control(
  input        clock,
  input        reset,
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
  output       io_regwrite,
  output       io_validinst
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
  assign io_validinst = 7'h33 == io_opcode | (7'h13 == io_opcode | (7'h3 == io_opcode | (7'h23 == io_opcode | (7'h63 ==
    io_opcode | (7'h37 == io_opcode | (7'h17 == io_opcode | (7'h6f == io_opcode | 7'h67 == io_opcode)))))));
endmodule
