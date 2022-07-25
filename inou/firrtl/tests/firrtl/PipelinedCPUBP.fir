circuit PipelinedCPUBP :
  module StageReg_2 : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip in : {ex_ctrl : {itype : UInt<1>, aluop : UInt<1>, resultselect : UInt<1>, xsrc : UInt<1>, ysrc : UInt<1>, plus4 : UInt<1>, branch : UInt<1>, jal : UInt<1>, jalr : UInt<1>, prediction : UInt<1>}, mem_ctrl : {memop : UInt<2>}, wb_ctrl : {toreg : UInt<1>, regwrite : UInt<1>}}, flip flush : UInt<1>, flip valid : UInt<1>, data : {ex_ctrl : {itype : UInt<1>, aluop : UInt<1>, resultselect : UInt<1>, xsrc : UInt<1>, ysrc : UInt<1>, plus4 : UInt<1>, branch : UInt<1>, jal : UInt<1>, jalr : UInt<1>, prediction : UInt<1>}, mem_ctrl : {memop : UInt<2>}, wb_ctrl : {toreg : UInt<1>, regwrite : UInt<1>}}}
    
    io.data.wb_ctrl.regwrite is invalid 
    io.data.wb_ctrl.toreg is invalid 
    io.data.mem_ctrl.memop is invalid 
    io.data.ex_ctrl.prediction is invalid 
    io.data.ex_ctrl.jalr is invalid 
    io.data.ex_ctrl.jal is invalid 
    io.data.ex_ctrl.branch is invalid 
    io.data.ex_ctrl.plus4 is invalid 
    io.data.ex_ctrl.ysrc is invalid 
    io.data.ex_ctrl.xsrc is invalid 
    io.data.ex_ctrl.resultselect is invalid 
    io.data.ex_ctrl.aluop is invalid 
    io.data.ex_ctrl.itype is invalid 
    io.valid is invalid 
    io.flush is invalid 
    io.in.wb_ctrl.regwrite is invalid 
    io.in.wb_ctrl.toreg is invalid 
    io.in.mem_ctrl.memop is invalid 
    io.in.ex_ctrl.prediction is invalid 
    io.in.ex_ctrl.jalr is invalid 
    io.in.ex_ctrl.jal is invalid 
    io.in.ex_ctrl.branch is invalid 
    io.in.ex_ctrl.plus4 is invalid 
    io.in.ex_ctrl.ysrc is invalid 
    io.in.ex_ctrl.xsrc is invalid 
    io.in.ex_ctrl.resultselect is invalid 
    io.in.ex_ctrl.aluop is invalid 
    io.in.ex_ctrl.itype is invalid 
    wire _T : {ex_ctrl : {itype : UInt<1>, aluop : UInt<1>, resultselect : UInt<1>, xsrc : UInt<1>, ysrc : UInt<1>, plus4 : UInt<1>, branch : UInt<1>, jal : UInt<1>, jalr : UInt<1>, prediction : UInt<1>}, mem_ctrl : {memop : UInt<2>}, wb_ctrl : {toreg : UInt<1>, regwrite : UInt<1>}} 
    _T.wb_ctrl.regwrite <= UInt<1>("h00") 
    _T.wb_ctrl.toreg <= UInt<1>("h00") 
    _T.mem_ctrl.memop <= UInt<2>("h00") 
    _T.ex_ctrl.prediction <= UInt<1>("h00") 
    _T.ex_ctrl.jalr <= UInt<1>("h00") 
    _T.ex_ctrl.jal <= UInt<1>("h00") 
    _T.ex_ctrl.branch <= UInt<1>("h00") 
    _T.ex_ctrl.plus4 <= UInt<1>("h00") 
    _T.ex_ctrl.ysrc <= UInt<1>("h00") 
    _T.ex_ctrl.xsrc <= UInt<1>("h00") 
    _T.ex_ctrl.resultselect <= UInt<1>("h00") 
    _T.ex_ctrl.aluop <= UInt<1>("h00") 
    _T.ex_ctrl.itype <= UInt<1>("h00") 
    reg reg : {ex_ctrl : {itype : UInt<1>, aluop : UInt<1>, resultselect : UInt<1>, xsrc : UInt<1>, ysrc : UInt<1>, plus4 : UInt<1>, branch : UInt<1>, jal : UInt<1>, jalr : UInt<1>, prediction : UInt<1>}, mem_ctrl : {memop : UInt<2>}, wb_ctrl : {toreg : UInt<1>, regwrite : UInt<1>}}, clock with : (reset => (reset, _T)) 
    io.data.wb_ctrl.regwrite <= reg.wb_ctrl.regwrite 
    io.data.wb_ctrl.toreg <= reg.wb_ctrl.toreg 
    io.data.mem_ctrl.memop <= reg.mem_ctrl.memop 
    io.data.ex_ctrl.prediction <= reg.ex_ctrl.prediction 
    io.data.ex_ctrl.jalr <= reg.ex_ctrl.jalr 
    io.data.ex_ctrl.jal <= reg.ex_ctrl.jal 
    io.data.ex_ctrl.branch <= reg.ex_ctrl.branch 
    io.data.ex_ctrl.plus4 <= reg.ex_ctrl.plus4 
    io.data.ex_ctrl.ysrc <= reg.ex_ctrl.ysrc 
    io.data.ex_ctrl.xsrc <= reg.ex_ctrl.xsrc 
    io.data.ex_ctrl.resultselect <= reg.ex_ctrl.resultselect 
    io.data.ex_ctrl.aluop <= reg.ex_ctrl.aluop 
    io.data.ex_ctrl.itype <= reg.ex_ctrl.itype 
    when io.valid : 
      reg.wb_ctrl.regwrite <= io.in.wb_ctrl.regwrite 
      reg.wb_ctrl.toreg <= io.in.wb_ctrl.toreg 
      reg.mem_ctrl.memop <= io.in.mem_ctrl.memop 
      reg.ex_ctrl.prediction <= io.in.ex_ctrl.prediction 
      reg.ex_ctrl.jalr <= io.in.ex_ctrl.jalr 
      reg.ex_ctrl.jal <= io.in.ex_ctrl.jal 
      reg.ex_ctrl.branch <= io.in.ex_ctrl.branch 
      reg.ex_ctrl.plus4 <= io.in.ex_ctrl.plus4 
      reg.ex_ctrl.ysrc <= io.in.ex_ctrl.ysrc 
      reg.ex_ctrl.xsrc <= io.in.ex_ctrl.xsrc 
      reg.ex_ctrl.resultselect <= io.in.ex_ctrl.resultselect 
      reg.ex_ctrl.aluop <= io.in.ex_ctrl.aluop 
      reg.ex_ctrl.itype <= io.in.ex_ctrl.itype 
      skip 
    when io.flush : 
      wire _T_1 : {ex_ctrl : {itype : UInt<1>, aluop : UInt<1>, resultselect : UInt<1>, xsrc : UInt<1>, ysrc : UInt<1>, plus4 : UInt<1>, branch : UInt<1>, jal : UInt<1>, jalr : UInt<1>, prediction : UInt<1>}, mem_ctrl : {memop : UInt<2>}, wb_ctrl : {toreg : UInt<1>, regwrite : UInt<1>}} 
      _T_1.wb_ctrl.regwrite <= UInt<1>("h00") 
      _T_1.wb_ctrl.toreg <= UInt<1>("h00") 
      _T_1.mem_ctrl.memop <= UInt<2>("h00") 
      _T_1.ex_ctrl.prediction <= UInt<1>("h00") 
      _T_1.ex_ctrl.jalr <= UInt<1>("h00") 
      _T_1.ex_ctrl.jal <= UInt<1>("h00") 
      _T_1.ex_ctrl.branch <= UInt<1>("h00") 
      _T_1.ex_ctrl.plus4 <= UInt<1>("h00") 
      _T_1.ex_ctrl.ysrc <= UInt<1>("h00") 
      _T_1.ex_ctrl.xsrc <= UInt<1>("h00") 
      _T_1.ex_ctrl.resultselect <= UInt<1>("h00") 
      _T_1.ex_ctrl.aluop <= UInt<1>("h00") 
      _T_1.ex_ctrl.itype <= UInt<1>("h00") 
      reg.wb_ctrl.regwrite <= _T_1.wb_ctrl.regwrite 
      reg.wb_ctrl.toreg <= _T_1.wb_ctrl.toreg 
      reg.mem_ctrl.memop <= _T_1.mem_ctrl.memop 
      reg.ex_ctrl.prediction <= _T_1.ex_ctrl.prediction 
      reg.ex_ctrl.jalr <= _T_1.ex_ctrl.jalr 
      reg.ex_ctrl.jal <= _T_1.ex_ctrl.jal 
      reg.ex_ctrl.branch <= _T_1.ex_ctrl.branch 
      reg.ex_ctrl.plus4 <= _T_1.ex_ctrl.plus4 
      reg.ex_ctrl.ysrc <= _T_1.ex_ctrl.ysrc 
      reg.ex_ctrl.xsrc <= _T_1.ex_ctrl.xsrc 
      reg.ex_ctrl.resultselect <= _T_1.ex_ctrl.resultselect 
      reg.ex_ctrl.aluop <= _T_1.ex_ctrl.aluop 
      reg.ex_ctrl.itype <= _T_1.ex_ctrl.itype 
      skip 
    
  module StageReg_4 : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip in : {mem_ctrl : {memop : UInt<2>}, wb_ctrl : {toreg : UInt<1>, regwrite : UInt<1>}}, flip flush : UInt<1>, flip valid : UInt<1>, data : {mem_ctrl : {memop : UInt<2>}, wb_ctrl : {toreg : UInt<1>, regwrite : UInt<1>}}}
    
    io.data.wb_ctrl.regwrite is invalid 
    io.data.wb_ctrl.toreg is invalid 
    io.data.mem_ctrl.memop is invalid 
    io.valid is invalid 
    io.flush is invalid 
    io.in.wb_ctrl.regwrite is invalid 
    io.in.wb_ctrl.toreg is invalid 
    io.in.mem_ctrl.memop is invalid 
    wire _T : {mem_ctrl : {memop : UInt<2>}, wb_ctrl : {toreg : UInt<1>, regwrite : UInt<1>}} 
    _T.wb_ctrl.regwrite <= UInt<1>("h00") 
    _T.wb_ctrl.toreg <= UInt<1>("h00") 
    _T.mem_ctrl.memop <= UInt<2>("h00") 
    reg reg : {mem_ctrl : {memop : UInt<2>}, wb_ctrl : {toreg : UInt<1>, regwrite : UInt<1>}}, clock with : (reset => (reset, _T)) 
    io.data.wb_ctrl.regwrite <= reg.wb_ctrl.regwrite 
    io.data.wb_ctrl.toreg <= reg.wb_ctrl.toreg 
    io.data.mem_ctrl.memop <= reg.mem_ctrl.memop 
    when io.valid : 
      reg.wb_ctrl.regwrite <= io.in.wb_ctrl.regwrite 
      reg.wb_ctrl.toreg <= io.in.wb_ctrl.toreg 
      reg.mem_ctrl.memop <= io.in.mem_ctrl.memop 
      skip 
    when io.flush : 
      wire _T_1 : {mem_ctrl : {memop : UInt<2>}, wb_ctrl : {toreg : UInt<1>, regwrite : UInt<1>}} 
      _T_1.wb_ctrl.regwrite <= UInt<1>("h00") 
      _T_1.wb_ctrl.toreg <= UInt<1>("h00") 
      _T_1.mem_ctrl.memop <= UInt<2>("h00") 
      reg.wb_ctrl.regwrite <= _T_1.wb_ctrl.regwrite 
      reg.wb_ctrl.toreg <= _T_1.wb_ctrl.toreg 
      reg.mem_ctrl.memop <= _T_1.mem_ctrl.memop 
      skip 
    
  module Control : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip opcode : UInt<7>, itype : UInt<1>, aluop : UInt<1>, xsrc : UInt<1>, ysrc : UInt<1>, branch : UInt<1>, jal : UInt<1>, jalr : UInt<1>, plus4 : UInt<1>, resultselect : UInt<1>, memop : UInt<2>, toreg : UInt<1>, regwrite : UInt<1>, validinst : UInt<1>}
    
    node _T = and(io.opcode, UInt<7>("h07f")) 
    node _T_1 = eq(UInt<6>("h033"), _T) 
    node _T_2 = and(io.opcode, UInt<7>("h07f")) 
    node _T_3 = eq(UInt<5>("h013"), _T_2) 
    node _T_4 = and(io.opcode, UInt<7>("h07f")) 
    node _T_5 = eq(UInt<2>("h03"), _T_4) 
    node _T_6 = and(io.opcode, UInt<7>("h07f")) 
    node _T_7 = eq(UInt<6>("h023"), _T_6) 
    node _T_8 = and(io.opcode, UInt<7>("h07f")) 
    node _T_9 = eq(UInt<7>("h063"), _T_8) 
    node _T_10 = and(io.opcode, UInt<7>("h07f")) 
    node _T_11 = eq(UInt<6>("h037"), _T_10) 
    node _T_12 = and(io.opcode, UInt<7>("h07f")) 
    node _T_13 = eq(UInt<5>("h017"), _T_12) 
    node _T_14 = and(io.opcode, UInt<7>("h07f")) 
    node _T_15 = eq(UInt<7>("h06f"), _T_14) 
    node _T_16 = and(io.opcode, UInt<7>("h07f")) 
    node _T_17 = eq(UInt<7>("h067"), _T_16) 
    node _T_18 = mux(_T_17, UInt<1>("h00"), UInt<1>("h00")) 
    node _T_19 = mux(_T_15, UInt<1>("h00"), _T_18) 
    node _T_20 = mux(_T_13, UInt<1>("h00"), _T_19) 
    node _T_21 = mux(_T_11, UInt<1>("h00"), _T_20) 
    node _T_22 = mux(_T_9, UInt<1>("h00"), _T_21) 
    node _T_23 = mux(_T_7, UInt<1>("h00"), _T_22) 
    node _T_24 = mux(_T_5, UInt<1>("h00"), _T_23) 
    node _T_25 = mux(_T_3, UInt<1>("h01"), _T_24) 
    node signals_0 = mux(_T_1, UInt<1>("h00"), _T_25) 
    node _T_26 = mux(_T_17, UInt<1>("h00"), UInt<1>("h00")) 
    node _T_27 = mux(_T_15, UInt<1>("h00"), _T_26) 
    node _T_28 = mux(_T_13, UInt<1>("h00"), _T_27) 
    node _T_29 = mux(_T_11, UInt<1>("h00"), _T_28) 
    node _T_30 = mux(_T_9, UInt<1>("h00"), _T_29) 
    node _T_31 = mux(_T_7, UInt<1>("h00"), _T_30) 
    node _T_32 = mux(_T_5, UInt<1>("h00"), _T_31) 
    node _T_33 = mux(_T_3, UInt<1>("h01"), _T_32) 
    node signals_1 = mux(_T_1, UInt<1>("h01"), _T_33) 
    node _T_34 = mux(_T_17, UInt<1>("h01"), UInt<1>("h00")) 
    node _T_35 = mux(_T_15, UInt<1>("h01"), _T_34) 
    node _T_36 = mux(_T_13, UInt<1>("h01"), _T_35) 
    node _T_37 = mux(_T_11, UInt<1>("h00"), _T_36) 
    node _T_38 = mux(_T_9, UInt<1>("h00"), _T_37) 
    node _T_39 = mux(_T_7, UInt<1>("h00"), _T_38) 
    node _T_40 = mux(_T_5, UInt<1>("h00"), _T_39) 
    node _T_41 = mux(_T_3, UInt<1>("h00"), _T_40) 
    node signals_2 = mux(_T_1, UInt<1>("h00"), _T_41) 
    node _T_42 = mux(_T_17, UInt<1>("h00"), UInt<1>("h00")) 
    node _T_43 = mux(_T_15, UInt<1>("h00"), _T_42) 
    node _T_44 = mux(_T_13, UInt<1>("h01"), _T_43) 
    node _T_45 = mux(_T_11, UInt<1>("h00"), _T_44) 
    node _T_46 = mux(_T_9, UInt<1>("h00"), _T_45) 
    node _T_47 = mux(_T_7, UInt<1>("h01"), _T_46) 
    node _T_48 = mux(_T_5, UInt<1>("h01"), _T_47) 
    node _T_49 = mux(_T_3, UInt<1>("h01"), _T_48) 
    node signals_3 = mux(_T_1, UInt<1>("h00"), _T_49) 
    node _T_50 = mux(_T_17, UInt<1>("h00"), UInt<1>("h00")) 
    node _T_51 = mux(_T_15, UInt<1>("h00"), _T_50) 
    node _T_52 = mux(_T_13, UInt<1>("h00"), _T_51) 
    node _T_53 = mux(_T_11, UInt<1>("h00"), _T_52) 
    node _T_54 = mux(_T_9, UInt<1>("h01"), _T_53) 
    node _T_55 = mux(_T_7, UInt<1>("h00"), _T_54) 
    node _T_56 = mux(_T_5, UInt<1>("h00"), _T_55) 
    node _T_57 = mux(_T_3, UInt<1>("h00"), _T_56) 
    node signals_4 = mux(_T_1, UInt<1>("h00"), _T_57) 
    node _T_58 = mux(_T_17, UInt<1>("h00"), UInt<1>("h00")) 
    node _T_59 = mux(_T_15, UInt<1>("h01"), _T_58) 
    node _T_60 = mux(_T_13, UInt<1>("h00"), _T_59) 
    node _T_61 = mux(_T_11, UInt<1>("h00"), _T_60) 
    node _T_62 = mux(_T_9, UInt<1>("h00"), _T_61) 
    node _T_63 = mux(_T_7, UInt<1>("h00"), _T_62) 
    node _T_64 = mux(_T_5, UInt<1>("h00"), _T_63) 
    node _T_65 = mux(_T_3, UInt<1>("h00"), _T_64) 
    node signals_5 = mux(_T_1, UInt<1>("h00"), _T_65) 
    node _T_66 = mux(_T_17, UInt<1>("h01"), UInt<1>("h00")) 
    node _T_67 = mux(_T_15, UInt<1>("h00"), _T_66) 
    node _T_68 = mux(_T_13, UInt<1>("h00"), _T_67) 
    node _T_69 = mux(_T_11, UInt<1>("h00"), _T_68) 
    node _T_70 = mux(_T_9, UInt<1>("h00"), _T_69) 
    node _T_71 = mux(_T_7, UInt<1>("h00"), _T_70) 
    node _T_72 = mux(_T_5, UInt<1>("h00"), _T_71) 
    node _T_73 = mux(_T_3, UInt<1>("h00"), _T_72) 
    node signals_6 = mux(_T_1, UInt<1>("h00"), _T_73) 
    node _T_74 = mux(_T_17, UInt<1>("h01"), UInt<1>("h00")) 
    node _T_75 = mux(_T_15, UInt<1>("h01"), _T_74) 
    node _T_76 = mux(_T_13, UInt<1>("h00"), _T_75) 
    node _T_77 = mux(_T_11, UInt<1>("h00"), _T_76) 
    node _T_78 = mux(_T_9, UInt<1>("h00"), _T_77) 
    node _T_79 = mux(_T_7, UInt<1>("h00"), _T_78) 
    node _T_80 = mux(_T_5, UInt<1>("h00"), _T_79) 
    node _T_81 = mux(_T_3, UInt<1>("h00"), _T_80) 
    node signals_7 = mux(_T_1, UInt<1>("h00"), _T_81) 
    node _T_82 = mux(_T_17, UInt<1>("h00"), UInt<1>("h00")) 
    node _T_83 = mux(_T_15, UInt<1>("h00"), _T_82) 
    node _T_84 = mux(_T_13, UInt<1>("h00"), _T_83) 
    node _T_85 = mux(_T_11, UInt<1>("h01"), _T_84) 
    node _T_86 = mux(_T_9, UInt<1>("h00"), _T_85) 
    node _T_87 = mux(_T_7, UInt<1>("h00"), _T_86) 
    node _T_88 = mux(_T_5, UInt<1>("h00"), _T_87) 
    node _T_89 = mux(_T_3, UInt<1>("h00"), _T_88) 
    node signals_8 = mux(_T_1, UInt<1>("h00"), _T_89) 
    node _T_90 = mux(_T_17, UInt<1>("h00"), UInt<1>("h00")) 
    node _T_91 = mux(_T_15, UInt<1>("h00"), _T_90) 
    node _T_92 = mux(_T_13, UInt<1>("h00"), _T_91) 
    node _T_93 = mux(_T_11, UInt<1>("h00"), _T_92) 
    node _T_94 = mux(_T_9, UInt<1>("h00"), _T_93) 
    node _T_95 = mux(_T_7, UInt<2>("h03"), _T_94) 
    node _T_96 = mux(_T_5, UInt<2>("h02"), _T_95) 
    node _T_97 = mux(_T_3, UInt<1>("h00"), _T_96) 
    node signals_9 = mux(_T_1, UInt<1>("h00"), _T_97) 
    node _T_98 = mux(_T_17, UInt<1>("h00"), UInt<1>("h00")) 
    node _T_99 = mux(_T_15, UInt<1>("h00"), _T_98) 
    node _T_100 = mux(_T_13, UInt<1>("h00"), _T_99) 
    node _T_101 = mux(_T_11, UInt<1>("h00"), _T_100) 
    node _T_102 = mux(_T_9, UInt<1>("h00"), _T_101) 
    node _T_103 = mux(_T_7, UInt<1>("h00"), _T_102) 
    node _T_104 = mux(_T_5, UInt<1>("h01"), _T_103) 
    node _T_105 = mux(_T_3, UInt<1>("h00"), _T_104) 
    node signals_10 = mux(_T_1, UInt<1>("h00"), _T_105) 
    node _T_106 = mux(_T_17, UInt<1>("h01"), UInt<1>("h00")) 
    node _T_107 = mux(_T_15, UInt<1>("h01"), _T_106) 
    node _T_108 = mux(_T_13, UInt<1>("h01"), _T_107) 
    node _T_109 = mux(_T_11, UInt<1>("h01"), _T_108) 
    node _T_110 = mux(_T_9, UInt<1>("h00"), _T_109) 
    node _T_111 = mux(_T_7, UInt<1>("h00"), _T_110) 
    node _T_112 = mux(_T_5, UInt<1>("h01"), _T_111) 
    node _T_113 = mux(_T_3, UInt<1>("h01"), _T_112) 
    node signals_11 = mux(_T_1, UInt<1>("h01"), _T_113) 
    node _T_114 = mux(_T_17, UInt<1>("h01"), UInt<1>("h00")) 
    node _T_115 = mux(_T_15, UInt<1>("h01"), _T_114) 
    node _T_116 = mux(_T_13, UInt<1>("h01"), _T_115) 
    node _T_117 = mux(_T_11, UInt<1>("h01"), _T_116) 
    node _T_118 = mux(_T_9, UInt<1>("h01"), _T_117) 
    node _T_119 = mux(_T_7, UInt<1>("h01"), _T_118) 
    node _T_120 = mux(_T_5, UInt<1>("h01"), _T_119) 
    node _T_121 = mux(_T_3, UInt<1>("h01"), _T_120) 
    node signals_12 = mux(_T_1, UInt<1>("h01"), _T_121) 
    io.itype <= signals_0 
    io.aluop <= signals_1 
    io.xsrc <= signals_2 
    io.ysrc <= signals_3 
    io.branch <= signals_4 
    io.jal <= signals_5 
    io.jalr <= signals_6 
    io.plus4 <= signals_7 
    io.resultselect <= signals_8 
    io.memop <= signals_9 
    io.toreg <= signals_10 
    io.regwrite <= signals_11 
    io.validinst <= signals_12 
    
  module Adder : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip inputx : UInt<32>, flip inputy : UInt<32>, result : UInt<32>}
    
    node _T = add(io.inputx, io.inputy) 
    node _T_1 = tail(_T, 1) 
    io.result <= _T_1 
    
  module ForwardingUnit : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip rs1 : UInt<5>, flip rs2 : UInt<5>, flip exmemrd : UInt<5>, flip exmemrw : UInt<1>, flip memwbrd : UInt<5>, flip memwbrw : UInt<1>, forwardA : UInt<2>, forwardB : UInt<2>}
    
    node _T = eq(io.exmemrw, UInt<1>("h01")) 
    node _T_1 = eq(io.exmemrd, io.rs1) 
    node _T_2 = and(_T, _T_1) 
    node _T_3 = neq(io.exmemrd, UInt<1>("h00")) 
    node _T_4 = and(_T_2, _T_3) 
    when _T_4 : 
      io.forwardA <= UInt<1>("h01") 
      skip 
    else : 
      node _T_5 = eq(io.memwbrw, UInt<1>("h01")) 
      node _T_6 = eq(io.memwbrd, io.rs1) 
      node _T_7 = and(_T_5, _T_6) 
      node _T_8 = neq(io.memwbrd, UInt<1>("h00")) 
      node _T_9 = and(_T_7, _T_8) 
      when _T_9 : 
        io.forwardA <= UInt<2>("h02") 
        skip 
      else : 
        io.forwardA <= UInt<1>("h00") 
        skip 
    node _T_10 = eq(io.exmemrw, UInt<1>("h01")) 
    node _T_11 = eq(io.exmemrd, io.rs2) 
    node _T_12 = and(_T_10, _T_11) 
    node _T_13 = neq(io.exmemrd, UInt<1>("h00")) 
    node _T_14 = and(_T_12, _T_13) 
    when _T_14 : 
      io.forwardB <= UInt<1>("h01") 
      skip 
    else : 
      node _T_15 = eq(io.memwbrw, UInt<1>("h01")) 
      node _T_16 = eq(io.memwbrd, io.rs2) 
      node _T_17 = and(_T_15, _T_16) 
      node _T_18 = neq(io.memwbrd, UInt<1>("h00")) 
      node _T_19 = and(_T_17, _T_18) 
      when _T_19 : 
        io.forwardB <= UInt<2>("h02") 
        skip 
      else : 
        io.forwardB <= UInt<1>("h00") 
        skip 
    
  module NextPC : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip branch : UInt<1>, flip jal : UInt<1>, flip jalr : UInt<1>, flip inputx : UInt<32>, flip inputy : UInt<32>, flip funct3 : UInt<3>, flip pc : UInt<32>, flip imm : UInt<32>, nextpc : UInt<32>, taken : UInt<1>}
    
    when io.branch : 
      node _T = eq(io.funct3, UInt<1>("h00")) 
      when _T : 
        node _T_1 = eq(io.inputx, io.inputy) 
        io.taken <= _T_1 
        skip 
      else : 
        node _T_2 = eq(io.funct3, UInt<1>("h01")) 
        when _T_2 : 
          node _T_3 = neq(io.inputx, io.inputy) 
          io.taken <= _T_3 
          skip 
        else : 
          node _T_4 = eq(io.funct3, UInt<3>("h04")) 
          when _T_4 : 
            node _T_5 = asSInt(io.inputx) 
            node _T_6 = asSInt(io.inputy) 
            node _T_7 = lt(_T_5, _T_6) 
            io.taken <= _T_7 
            skip 
          else : 
            node _T_8 = eq(io.funct3, UInt<3>("h05")) 
            when _T_8 : 
              node _T_9 = asSInt(io.inputx) 
              node _T_10 = asSInt(io.inputy) 
              node _T_11 = geq(_T_9, _T_10) 
              io.taken <= _T_11 
              skip 
            else : 
              node _T_12 = eq(io.funct3, UInt<3>("h06")) 
              when _T_12 : 
                node _T_13 = lt(io.inputx, io.inputy) 
                io.taken <= _T_13 
                skip 
              else : 
                node _T_14 = eq(io.funct3, UInt<3>("h07")) 
                when _T_14 : 
                  node _T_15 = geq(io.inputx, io.inputy) 
                  io.taken <= _T_15 
                  skip 
                else : 
                  io.taken <= UInt<1>("h00") 
                  skip 
      when io.taken : 
        node _T_16 = add(io.pc, io.imm) 
        node _T_17 = tail(_T_16, 1) 
        io.nextpc <= _T_17 
        skip 
      else : 
        node _T_18 = add(io.pc, UInt<3>("h04")) 
        node _T_19 = tail(_T_18, 1) 
        io.nextpc <= _T_19 
        skip 
      skip 
    else : 
      when io.jal : 
        io.taken <= UInt<1>("h01") 
        node _T_20 = add(io.pc, io.imm) 
        node _T_21 = tail(_T_20, 1) 
        io.nextpc <= _T_21 
        skip 
      else : 
        when io.jalr : 
          io.taken <= UInt<1>("h01") 
          node _T_22 = add(io.inputx, io.imm) 
          node _T_23 = tail(_T_22, 1) 
          io.nextpc <= _T_23 
          skip 
        else : 
          node _T_24 = add(io.pc, UInt<3>("h04")) 
          node _T_25 = tail(_T_24, 1) 
          io.nextpc <= _T_25 
          io.taken <= UInt<1>("h00") 
          skip 
    
  module RegisterFile : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip readreg1 : UInt<5>, flip readreg2 : UInt<5>, flip writereg : UInt<5>, flip writedata : UInt<32>, flip wen : UInt<1>, readdata1 : UInt<32>, readdata2 : UInt<32>}
    
    reg regs : UInt<32>[32], clock 
    when io.wen : 
      regs[io.writereg] <= io.writedata 
      skip 
    io.readdata1 <= regs[io.readreg1] 
    io.readdata2 <= regs[io.readreg2] 
    node _T = eq(io.readreg1, io.writereg) 
    node _T_1 = and(_T, io.wen) 
    when _T_1 : 
      io.readdata1 <= io.writedata 
      skip 
    else : 
      node _T_2 = eq(io.readreg2, io.writereg) 
      node _T_3 = and(_T_2, io.wen) 
      when _T_3 : 
        io.readdata2 <= io.writedata 
        skip 
    
  module StageReg_1 : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip in : {pc : UInt<32>, instruction : UInt<32>, sextImm : UInt<32>, readdata1 : UInt<32>, readdata2 : UInt<32>}, flip flush : UInt<1>, flip valid : UInt<1>, data : {pc : UInt<32>, instruction : UInt<32>, sextImm : UInt<32>, readdata1 : UInt<32>, readdata2 : UInt<32>}}
    
    io.data.readdata2 is invalid 
    io.data.readdata1 is invalid 
    io.data.sextImm is invalid 
    io.data.instruction is invalid 
    io.data.pc is invalid 
    io.valid is invalid 
    io.flush is invalid 
    io.in.readdata2 is invalid 
    io.in.readdata1 is invalid 
    io.in.sextImm is invalid 
    io.in.instruction is invalid 
    io.in.pc is invalid 
    wire _T : {pc : UInt<32>, instruction : UInt<32>, sextImm : UInt<32>, readdata1 : UInt<32>, readdata2 : UInt<32>} 
    _T.readdata2 <= UInt<32>("h00") 
    _T.readdata1 <= UInt<32>("h00") 
    _T.sextImm <= UInt<32>("h00") 
    _T.instruction <= UInt<32>("h00") 
    _T.pc <= UInt<32>("h00") 
    reg reg : {pc : UInt<32>, instruction : UInt<32>, sextImm : UInt<32>, readdata1 : UInt<32>, readdata2 : UInt<32>}, clock with : (reset => (reset, _T)) 
    io.data.readdata2 <= reg.readdata2 
    io.data.readdata1 <= reg.readdata1 
    io.data.sextImm <= reg.sextImm 
    io.data.instruction <= reg.instruction 
    io.data.pc <= reg.pc 
    when io.valid : 
      reg.readdata2 <= io.in.readdata2 
      reg.readdata1 <= io.in.readdata1 
      reg.sextImm <= io.in.sextImm 
      reg.instruction <= io.in.instruction 
      reg.pc <= io.in.pc 
      skip 
    when io.flush : 
      wire _T_1 : {pc : UInt<32>, instruction : UInt<32>, sextImm : UInt<32>, readdata1 : UInt<32>, readdata2 : UInt<32>} 
      _T_1.readdata2 <= UInt<32>("h00") 
      _T_1.readdata1 <= UInt<32>("h00") 
      _T_1.sextImm <= UInt<32>("h00") 
      _T_1.instruction <= UInt<32>("h00") 
      _T_1.pc <= UInt<32>("h00") 
      reg.readdata2 <= _T_1.readdata2 
      reg.readdata1 <= _T_1.readdata1 
      reg.sextImm <= _T_1.sextImm 
      reg.instruction <= _T_1.instruction 
      reg.pc <= _T_1.pc 
      skip 
    
  module StageReg_5 : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip in : {instruction : UInt<32>, readdata : UInt<32>, ex_result : UInt<32>}, flip flush : UInt<1>, flip valid : UInt<1>, data : {instruction : UInt<32>, readdata : UInt<32>, ex_result : UInt<32>}}
    
    io.data.ex_result is invalid 
    io.data.readdata is invalid 
    io.data.instruction is invalid 
    io.valid is invalid 
    io.flush is invalid 
    io.in.ex_result is invalid 
    io.in.readdata is invalid 
    io.in.instruction is invalid 
    wire _T : {instruction : UInt<32>, readdata : UInt<32>, ex_result : UInt<32>} 
    _T.ex_result <= UInt<32>("h00") 
    _T.readdata <= UInt<32>("h00") 
    _T.instruction <= UInt<32>("h00") 
    reg reg : {instruction : UInt<32>, readdata : UInt<32>, ex_result : UInt<32>}, clock with : (reset => (reset, _T)) 
    io.data.ex_result <= reg.ex_result 
    io.data.readdata <= reg.readdata 
    io.data.instruction <= reg.instruction 
    when io.valid : 
      reg.ex_result <= io.in.ex_result 
      reg.readdata <= io.in.readdata 
      reg.instruction <= io.in.instruction 
      skip 
    when io.flush : 
      wire _T_1 : {instruction : UInt<32>, readdata : UInt<32>, ex_result : UInt<32>} 
      _T_1.ex_result <= UInt<32>("h00") 
      _T_1.readdata <= UInt<32>("h00") 
      _T_1.instruction <= UInt<32>("h00") 
      reg.ex_result <= _T_1.ex_result 
      reg.readdata <= _T_1.readdata 
      reg.instruction <= _T_1.instruction 
      skip 
    
  module StageReg_3 : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip in : {ex_result : UInt<32>, mem_writedata : UInt<32>, instruction : UInt<32>, next_pc : UInt<32>, taken : UInt<1>}, flip flush : UInt<1>, flip valid : UInt<1>, data : {ex_result : UInt<32>, mem_writedata : UInt<32>, instruction : UInt<32>, next_pc : UInt<32>, taken : UInt<1>}}
    
    io.data.taken is invalid 
    io.data.next_pc is invalid 
    io.data.instruction is invalid 
    io.data.mem_writedata is invalid 
    io.data.ex_result is invalid 
    io.valid is invalid 
    io.flush is invalid 
    io.in.taken is invalid 
    io.in.next_pc is invalid 
    io.in.instruction is invalid 
    io.in.mem_writedata is invalid 
    io.in.ex_result is invalid 
    wire _T : {ex_result : UInt<32>, mem_writedata : UInt<32>, instruction : UInt<32>, next_pc : UInt<32>, taken : UInt<1>} 
    _T.taken <= UInt<1>("h00") 
    _T.next_pc <= UInt<32>("h00") 
    _T.instruction <= UInt<32>("h00") 
    _T.mem_writedata <= UInt<32>("h00") 
    _T.ex_result <= UInt<32>("h00") 
    reg reg : {ex_result : UInt<32>, mem_writedata : UInt<32>, instruction : UInt<32>, next_pc : UInt<32>, taken : UInt<1>}, clock with : (reset => (reset, _T)) 
    io.data.taken <= reg.taken 
    io.data.next_pc <= reg.next_pc 
    io.data.instruction <= reg.instruction 
    io.data.mem_writedata <= reg.mem_writedata 
    io.data.ex_result <= reg.ex_result 
    when io.valid : 
      reg.taken <= io.in.taken 
      reg.next_pc <= io.in.next_pc 
      reg.instruction <= io.in.instruction 
      reg.mem_writedata <= io.in.mem_writedata 
      reg.ex_result <= io.in.ex_result 
      skip 
    when io.flush : 
      wire _T_1 : {ex_result : UInt<32>, mem_writedata : UInt<32>, instruction : UInt<32>, next_pc : UInt<32>, taken : UInt<1>} 
      _T_1.taken <= UInt<1>("h00") 
      _T_1.next_pc <= UInt<32>("h00") 
      _T_1.instruction <= UInt<32>("h00") 
      _T_1.mem_writedata <= UInt<32>("h00") 
      _T_1.ex_result <= UInt<32>("h00") 
      reg.taken <= _T_1.taken 
      reg.next_pc <= _T_1.next_pc 
      reg.instruction <= _T_1.instruction 
      reg.mem_writedata <= _T_1.mem_writedata 
      reg.ex_result <= _T_1.ex_result 
      skip 
    
  module StageReg_6 : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip in : {wb_ctrl : {toreg : UInt<1>, regwrite : UInt<1>}}, flip flush : UInt<1>, flip valid : UInt<1>, data : {wb_ctrl : {toreg : UInt<1>, regwrite : UInt<1>}}}
    
    io.data.wb_ctrl.regwrite is invalid 
    io.data.wb_ctrl.toreg is invalid 
    io.valid is invalid 
    io.flush is invalid 
    io.in.wb_ctrl.regwrite is invalid 
    io.in.wb_ctrl.toreg is invalid 
    wire _T : {wb_ctrl : {toreg : UInt<1>, regwrite : UInt<1>}} 
    _T.wb_ctrl.regwrite <= UInt<1>("h00") 
    _T.wb_ctrl.toreg <= UInt<1>("h00") 
    reg reg : {wb_ctrl : {toreg : UInt<1>, regwrite : UInt<1>}}, clock with : (reset => (reset, _T)) 
    io.data.wb_ctrl.regwrite <= reg.wb_ctrl.regwrite 
    io.data.wb_ctrl.toreg <= reg.wb_ctrl.toreg 
    when io.valid : 
      reg.wb_ctrl.regwrite <= io.in.wb_ctrl.regwrite 
      reg.wb_ctrl.toreg <= io.in.wb_ctrl.toreg 
      skip 
    when io.flush : 
      wire _T_1 : {wb_ctrl : {toreg : UInt<1>, regwrite : UInt<1>}} 
      _T_1.wb_ctrl.regwrite <= UInt<1>("h00") 
      _T_1.wb_ctrl.toreg <= UInt<1>("h00") 
      reg.wb_ctrl.regwrite <= _T_1.wb_ctrl.regwrite 
      reg.wb_ctrl.toreg <= _T_1.wb_ctrl.toreg 
      skip 
    
  module ALU : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip operation : UInt<4>, flip inputx : UInt<32>, flip inputy : UInt<32>, result : UInt<32>}
    
    node _T = eq(io.operation, UInt<3>("h06")) 
    when _T : 
      node _T_1 = and(io.inputx, io.inputy) 
      io.result <= _T_1 
      skip 
    else : 
      node _T_2 = eq(io.operation, UInt<3>("h05")) 
      when _T_2 : 
        node _T_3 = or(io.inputx, io.inputy) 
        io.result <= _T_3 
        skip 
      else : 
        node _T_4 = eq(io.operation, UInt<3>("h07")) 
        when _T_4 : 
          node _T_5 = add(io.inputx, io.inputy) 
          node _T_6 = tail(_T_5, 1) 
          io.result <= _T_6 
          skip 
        else : 
          node _T_7 = eq(io.operation, UInt<3>("h04")) 
          when _T_7 : 
            node _T_8 = sub(io.inputx, io.inputy) 
            node _T_9 = tail(_T_8, 1) 
            io.result <= _T_9 
            skip 
          else : 
            node _T_10 = eq(io.operation, UInt<2>("h03")) 
            when _T_10 : 
              node _T_11 = asSInt(io.inputx) 
              node _T_12 = bits(io.inputy, 4, 0) 
              node _T_13 = dshr(_T_11, _T_12) 
              node _T_14 = asUInt(_T_13) 
              io.result <= _T_14 
              skip 
            else : 
              node _T_15 = eq(io.operation, UInt<1>("h01")) 
              when _T_15 : 
                node _T_16 = lt(io.inputx, io.inputy) 
                io.result <= _T_16 
                skip 
              else : 
                node _T_17 = eq(io.operation, UInt<1>("h00")) 
                when _T_17 : 
                  node _T_18 = xor(io.inputx, io.inputy) 
                  io.result <= _T_18 
                  skip 
                else : 
                  node _T_19 = eq(io.operation, UInt<2>("h02")) 
                  when _T_19 : 
                    node _T_20 = bits(io.inputy, 4, 0) 
                    node _T_21 = dshr(io.inputx, _T_20) 
                    io.result <= _T_21 
                    skip 
                  else : 
                    node _T_22 = eq(io.operation, UInt<4>("h09")) 
                    when _T_22 : 
                      node _T_23 = asSInt(io.inputx) 
                      node _T_24 = asSInt(io.inputy) 
                      node _T_25 = lt(_T_23, _T_24) 
                      io.result <= _T_25 
                      skip 
                    else : 
                      node _T_26 = eq(io.operation, UInt<4>("h08")) 
                      when _T_26 : 
                        node _T_27 = bits(io.inputy, 4, 0) 
                        node _T_28 = dshl(io.inputx, _T_27) 
                        io.result <= _T_28 
                        skip 
                      else : 
                        node _T_29 = eq(io.operation, UInt<4>("h0a")) 
                        when _T_29 : 
                          node _T_30 = or(io.inputx, io.inputy) 
                          node _T_31 = not(_T_30) 
                          io.result <= _T_31 
                          skip 
                        else : 
                          node _T_32 = eq(io.operation, UInt<4>("h0b")) 
                          when _T_32 : 
                            node _T_33 = asSInt(io.inputx) 
                            node _T_34 = asSInt(io.inputy) 
                            node _T_35 = geq(_T_33, _T_34) 
                            io.result <= _T_35 
                            skip 
                          else : 
                            node _T_36 = eq(io.operation, UInt<4>("h0c")) 
                            when _T_36 : 
                              node _T_37 = geq(io.inputx, io.inputy) 
                              io.result <= _T_37 
                              skip 
                            else : 
                              node _T_38 = eq(io.operation, UInt<4>("h0d")) 
                              when _T_38 : 
                                node _T_39 = eq(io.inputx, io.inputy) 
                                io.result <= _T_39 
                                skip 
                              else : 
                                node _T_40 = eq(io.operation, UInt<4>("h0e")) 
                                when _T_40 : 
                                  node _T_41 = neq(io.inputx, io.inputy) 
                                  io.result <= _T_41 
                                  skip 
                                else : 
                                  io.result <= UInt<1>("h00") 
                                  skip 
    
  module ImmediateGenerator : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip instruction : UInt<32>, sextImm : UInt<32>}
    
    io.sextImm <= UInt<1>("h00") 
    node opcode = bits(io.instruction, 6, 0) 
    node _T = eq(UInt<6>("h037"), opcode) 
    when _T : 
      node _T_1 = bits(io.instruction, 31, 12) 
      node _T_2 = mux(UInt<1>("h00"), UInt<12>("h0fff"), UInt<12>("h00")) 
      node _T_3 = cat(_T_1, _T_2) 
      io.sextImm <= _T_3 
      skip 
    else : 
      node _T_4 = eq(UInt<5>("h017"), opcode) 
      when _T_4 : 
        node _T_5 = bits(io.instruction, 31, 12) 
        node _T_6 = mux(UInt<1>("h00"), UInt<12>("h0fff"), UInt<12>("h00")) 
        node _T_7 = cat(_T_5, _T_6) 
        io.sextImm <= _T_7 
        skip 
      else : 
        node _T_8 = eq(UInt<7>("h06f"), opcode) 
        when _T_8 : 
          node _T_9 = bits(io.instruction, 31, 31) 
          node _T_10 = bits(io.instruction, 19, 12) 
          node _T_11 = bits(io.instruction, 20, 20) 
          node _T_12 = bits(io.instruction, 30, 21) 
          node _T_13 = cat(_T_11, _T_12) 
          node _T_14 = cat(_T_9, _T_10) 
          node _T_15 = cat(_T_14, _T_13) 
          node _T_16 = bits(_T_15, 19, 19) 
          node _T_17 = bits(_T_16, 0, 0) 
          node _T_18 = mux(_T_17, UInt<11>("h07ff"), UInt<11>("h00")) 
          node _T_19 = cat(_T_18, _T_15) 
          node _T_20 = cat(_T_19, UInt<1>("h00")) 
          io.sextImm <= _T_20 
          skip 
        else : 
          node _T_21 = eq(UInt<7>("h067"), opcode) 
          when _T_21 : 
            node _T_22 = bits(io.instruction, 31, 20) 
            node _T_23 = bits(_T_22, 11, 11) 
            node _T_24 = bits(_T_23, 0, 0) 
            node _T_25 = mux(_T_24, UInt<20>("h0fffff"), UInt<20>("h00")) 
            node _T_26 = cat(_T_25, _T_22) 
            io.sextImm <= _T_26 
            skip 
          else : 
            node _T_27 = eq(UInt<7>("h063"), opcode) 
            when _T_27 : 
              node _T_28 = bits(io.instruction, 31, 31) 
              node _T_29 = bits(io.instruction, 7, 7) 
              node _T_30 = bits(io.instruction, 30, 25) 
              node _T_31 = bits(io.instruction, 11, 8) 
              node _T_32 = cat(_T_30, _T_31) 
              node _T_33 = cat(_T_28, _T_29) 
              node _T_34 = cat(_T_33, _T_32) 
              node _T_35 = bits(_T_34, 11, 11) 
              node _T_36 = bits(_T_35, 0, 0) 
              node _T_37 = mux(_T_36, UInt<19>("h07ffff"), UInt<19>("h00")) 
              node _T_38 = cat(_T_37, _T_34) 
              node _T_39 = cat(_T_38, UInt<1>("h00")) 
              io.sextImm <= _T_39 
              skip 
            else : 
              node _T_40 = eq(UInt<2>("h03"), opcode) 
              when _T_40 : 
                node _T_41 = bits(io.instruction, 31, 20) 
                node _T_42 = bits(_T_41, 11, 11) 
                node _T_43 = bits(_T_42, 0, 0) 
                node _T_44 = mux(_T_43, UInt<20>("h0fffff"), UInt<20>("h00")) 
                node _T_45 = cat(_T_44, _T_41) 
                io.sextImm <= _T_45 
                skip 
              else : 
                node _T_46 = eq(UInt<6>("h023"), opcode) 
                when _T_46 : 
                  node _T_47 = bits(io.instruction, 31, 25) 
                  node _T_48 = bits(io.instruction, 11, 7) 
                  node _T_49 = cat(_T_47, _T_48) 
                  node _T_50 = bits(_T_49, 11, 11) 
                  node _T_51 = bits(_T_50, 0, 0) 
                  node _T_52 = mux(_T_51, UInt<20>("h0fffff"), UInt<20>("h00")) 
                  node _T_53 = cat(_T_52, _T_49) 
                  io.sextImm <= _T_53 
                  skip 
                else : 
                  node _T_54 = eq(UInt<5>("h013"), opcode) 
                  when _T_54 : 
                    node _T_55 = bits(io.instruction, 31, 20) 
                    node _T_56 = bits(_T_55, 11, 11) 
                    node _T_57 = bits(_T_56, 0, 0) 
                    node _T_58 = mux(_T_57, UInt<20>("h0fffff"), UInt<20>("h00")) 
                    node _T_59 = cat(_T_58, _T_55) 
                    io.sextImm <= _T_59 
                    skip 
                  else : 
                    node _T_60 = eq(UInt<7>("h073"), opcode) 
                    when _T_60 : 
                      node _T_61 = mux(UInt<1>("h00"), UInt<27>("h07ffffff"), UInt<27>("h00")) 
                      node _T_62 = bits(io.instruction, 19, 15) 
                      node _T_63 = cat(_T_61, _T_62) 
                      io.sextImm <= _T_63 
                      skip 
    
  module HazardUnitBP : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip rs1 : UInt<5>, flip rs2 : UInt<5>, flip id_prediction : UInt<1>, flip idex_memread : UInt<1>, flip idex_rd : UInt<5>, flip exmem_taken : UInt<1>, pcSel : UInt<2>, if_id_stall : UInt<1>, if_id_flush : UInt<1>, id_ex_flush : UInt<1>, ex_mem_flush : UInt<1>}
    
    io.pcSel <= UInt<1>("h00") 
    io.if_id_stall <= UInt<1>("h00") 
    io.id_ex_flush <= UInt<1>("h00") 
    io.ex_mem_flush <= UInt<1>("h00") 
    io.if_id_flush <= UInt<1>("h00") 
    node _T = eq(io.exmem_taken, UInt<1>("h01")) 
    when _T : 
      io.pcSel <= UInt<1>("h01") 
      io.if_id_flush <= UInt<1>("h01") 
      io.id_ex_flush <= UInt<1>("h01") 
      io.ex_mem_flush <= UInt<1>("h01") 
      skip 
    else : 
      node _T_1 = eq(io.idex_memread, UInt<1>("h01")) 
      node _T_2 = eq(io.idex_rd, io.rs1) 
      node _T_3 = eq(io.idex_rd, io.rs2) 
      node _T_4 = or(_T_2, _T_3) 
      node _T_5 = and(_T_1, _T_4) 
      when _T_5 : 
        io.pcSel <= UInt<2>("h03") 
        io.if_id_stall <= UInt<1>("h01") 
        io.id_ex_flush <= UInt<1>("h01") 
        skip 
      else : 
        node _T_6 = eq(io.id_prediction, UInt<1>("h01")) 
        when _T_6 : 
          io.pcSel <= UInt<2>("h02") 
          io.if_id_flush <= UInt<1>("h01") 
          skip 
        else : 
          io.pcSel <= UInt<1>("h00") 
          io.if_id_stall <= UInt<1>("h00") 
          io.id_ex_flush <= UInt<1>("h00") 
          io.ex_mem_flush <= UInt<1>("h00") 
          io.if_id_flush <= UInt<1>("h00") 
          skip 
    
  module StageReg : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip in : {instruction : UInt<32>, pc : UInt<32>}, flip flush : UInt<1>, flip valid : UInt<1>, data : {instruction : UInt<32>, pc : UInt<32>}}
    
    io.data.pc is invalid 
    io.data.instruction is invalid 
    io.valid is invalid 
    io.flush is invalid 
    io.in.pc is invalid 
    io.in.instruction is invalid 
    wire _T : {instruction : UInt<32>, pc : UInt<32>} 
    _T.pc <= UInt<32>("h00") 
    _T.instruction <= UInt<32>("h00") 
    reg reg : {instruction : UInt<32>, pc : UInt<32>}, clock with : (reset => (reset, _T)) 
    io.data.pc <= reg.pc 
    io.data.instruction <= reg.instruction 
    when io.valid : 
      reg.pc <= io.in.pc 
      reg.instruction <= io.in.instruction 
      skip 
    when io.flush : 
      wire _T_1 : {instruction : UInt<32>, pc : UInt<32>} 
      _T_1.pc <= UInt<32>("h00") 
      _T_1.instruction <= UInt<32>("h00") 
      reg.pc <= _T_1.pc 
      reg.instruction <= _T_1.instruction 
      skip 
    
  module AlwaysNotTakenPredictor : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip pc : UInt<32>, flip update : UInt<1>, flip taken : UInt<1>, prediction : UInt<1>}
    
    wire _T : UInt<2>[32] 
    _T[0] <= UInt<2>("h02") 
    _T[1] <= UInt<2>("h02") 
    _T[2] <= UInt<2>("h02") 
    _T[3] <= UInt<2>("h02") 
    _T[4] <= UInt<2>("h02") 
    _T[5] <= UInt<2>("h02") 
    _T[6] <= UInt<2>("h02") 
    _T[7] <= UInt<2>("h02") 
    _T[8] <= UInt<2>("h02") 
    _T[9] <= UInt<2>("h02") 
    _T[10] <= UInt<2>("h02") 
    _T[11] <= UInt<2>("h02") 
    _T[12] <= UInt<2>("h02") 
    _T[13] <= UInt<2>("h02") 
    _T[14] <= UInt<2>("h02") 
    _T[15] <= UInt<2>("h02") 
    _T[16] <= UInt<2>("h02") 
    _T[17] <= UInt<2>("h02") 
    _T[18] <= UInt<2>("h02") 
    _T[19] <= UInt<2>("h02") 
    _T[20] <= UInt<2>("h02") 
    _T[21] <= UInt<2>("h02") 
    _T[22] <= UInt<2>("h02") 
    _T[23] <= UInt<2>("h02") 
    _T[24] <= UInt<2>("h02") 
    _T[25] <= UInt<2>("h02") 
    _T[26] <= UInt<2>("h02") 
    _T[27] <= UInt<2>("h02") 
    _T[28] <= UInt<2>("h02") 
    _T[29] <= UInt<2>("h02") 
    _T[30] <= UInt<2>("h02") 
    _T[31] <= UInt<2>("h02") 
    reg predictionTable : UInt<2>[32], clock with : (reset => (reset, _T)) 
    io.prediction <= UInt<1>("h00") 
    
  module ALUControl : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip aluop : UInt<1>, flip itype : UInt<1>, flip funct7 : UInt<7>, flip funct3 : UInt<3>, operation : UInt<4>}
    
    when io.aluop : 
      node _T = eq(io.funct3, UInt<1>("h00")) 
      when _T : 
        node _T_1 = eq(io.funct7, UInt<1>("h00")) 
        node _T_2 = or(io.itype, _T_1) 
        when _T_2 : 
          io.operation <= UInt<3>("h07") 
          skip 
        else : 
          io.operation <= UInt<3>("h04") 
          skip 
        skip 
      else : 
        node _T_3 = eq(io.funct3, UInt<1>("h01")) 
        when _T_3 : 
          io.operation <= UInt<4>("h08") 
          skip 
        else : 
          node _T_4 = eq(io.funct3, UInt<2>("h02")) 
          when _T_4 : 
            io.operation <= UInt<4>("h09") 
            skip 
          else : 
            node _T_5 = eq(io.funct3, UInt<2>("h03")) 
            when _T_5 : 
              io.operation <= UInt<1>("h01") 
              skip 
            else : 
              node _T_6 = eq(io.funct3, UInt<3>("h04")) 
              when _T_6 : 
                io.operation <= UInt<1>("h00") 
                skip 
              else : 
                node _T_7 = eq(io.funct3, UInt<3>("h05")) 
                when _T_7 : 
                  node _T_8 = eq(io.funct7, UInt<1>("h00")) 
                  when _T_8 : 
                    io.operation <= UInt<2>("h02") 
                    skip 
                  else : 
                    io.operation <= UInt<2>("h03") 
                    skip 
                  skip 
                else : 
                  node _T_9 = eq(io.funct3, UInt<3>("h06")) 
                  when _T_9 : 
                    io.operation <= UInt<3>("h05") 
                    skip 
                  else : 
                    io.operation <= UInt<3>("h06") 
                    skip 
      skip 
    else : 
      io.operation <= UInt<3>("h07") 
      skip 
    
  module Adder_1 : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip inputx : UInt<32>, flip inputy : UInt<32>, result : UInt<32>}
    
    node _T = add(io.inputx, io.inputy) 
    node _T_1 = tail(_T, 1) 
    io.result <= _T_1 
    
  module PipelinedCPUBP : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip imem : {flip address : UInt<32>, flip valid : UInt<1>, good : UInt<1>, instruction : UInt<32>, ready : UInt<1>}, flip dmem : {flip address : UInt<32>, flip valid : UInt<1>, good : UInt<1>, flip writedata : UInt<32>, flip memread : UInt<1>, flip memwrite : UInt<1>, flip maskmode : UInt<2>, flip sext : UInt<1>, readdata : UInt<32>}}
    
    reg pc : UInt, clock with : (reset => (reset, UInt<1>("h00"))) 
    inst control of Control 
    control.clock <= clock
    control.reset <= reset
    inst registers of RegisterFile 
    registers.clock <= clock
    registers.reset <= reset
    inst aluControl of ALUControl 
    aluControl.clock <= clock
    aluControl.reset <= reset
    inst alu of ALU 
    alu.clock <= clock
    alu.reset <= reset
    inst immGen of ImmediateGenerator 
    immGen.clock <= clock
    immGen.reset <= reset
    inst pcPlusFour of Adder 
    pcPlusFour.clock <= clock
    pcPlusFour.reset <= reset
    inst nextPCmod of NextPC 
    nextPCmod.clock <= clock
    nextPCmod.reset <= reset
    inst forwarding of ForwardingUnit 
    forwarding.clock <= clock
    forwarding.reset <= reset
    inst hazard of HazardUnitBP 
    hazard.clock <= clock
    hazard.reset <= reset
    inst predictor of AlwaysNotTakenPredictor 
    predictor.clock <= clock
    predictor.reset <= reset
    inst branchAdd of Adder_1 
    branchAdd.clock <= clock
    branchAdd.reset <= reset
    reg value : UInt<30>, clock with : (reset => (reset, UInt<30>("h00"))) 
    wire _T : UInt<1>
    _T <= UInt<1>("h00")
    when UInt<1>("h01") : 
      node _T_1 = eq(value, UInt<30>("h03fffffff")) 
      node _T_2 = add(value, UInt<1>("h01")) 
      node _T_3 = tail(_T_2, 1) 
      value <= _T_3 
      _T <= _T_1 
      skip 
    inst if_id of StageReg 
    if_id.clock <= clock
    if_id.reset <= reset
    inst id_ex of StageReg_1 
    id_ex.clock <= clock
    id_ex.reset <= reset
    inst id_ex_ctrl of StageReg_2 
    id_ex_ctrl.clock <= clock
    id_ex_ctrl.reset <= reset
    inst ex_mem of StageReg_3 
    ex_mem.clock <= clock
    ex_mem.reset <= reset
    inst ex_mem_ctrl of StageReg_4 
    ex_mem_ctrl.clock <= clock
    ex_mem_ctrl.reset <= reset
    inst mem_wb of StageReg_5 
    mem_wb.clock <= clock
    mem_wb.reset <= reset
    inst mem_wb_ctrl of StageReg_6 
    mem_wb_ctrl.clock <= clock
    mem_wb_ctrl.reset <= reset
    reg bpCorrect : UInt<32>, clock with : (reset => (reset, UInt<32>("h00"))) 
    reg bpIncorrect : UInt<32>, clock with : (reset => (reset, UInt<32>("h00"))) 
    node _T_4 = shl(UInt<1>("h01"), 20) 
    node _T_5 = gt(bpCorrect, _T_4) 
    when _T_5 : 
      node _T_6 = asUInt(reset) 
      node _T_7 = eq(_T_6, UInt<1>("h00")) 
      when _T_7 : 
        printf(clock, UInt<1>(1), "BP correct: %d; incorrect: %d\n", bpCorrect, bpIncorrect) 
        skip 
      skip 
    wire next_pc : UInt<32> 
    wire id_next_pc : UInt 
    wire write_data : UInt 
    node _T_8 = eq(hazard.io.pcSel, UInt<1>("h00")) 
    node _T_9 = eq(hazard.io.pcSel, UInt<1>("h01")) 
    node _T_10 = eq(hazard.io.pcSel, UInt<2>("h02")) 
    node _T_11 = eq(hazard.io.pcSel, UInt<2>("h03")) 
    node _T_12 = mux(_T_11, pc, UInt<1>("h00")) 
    node _T_13 = mux(_T_10, id_next_pc, _T_12) 
    node _T_14 = mux(_T_9, next_pc, _T_13) 
    node _T_15 = mux(_T_8, pcPlusFour.io.result, _T_14) 
    pc <= _T_15 
    io.imem.address <= pc 
    io.imem.valid <= UInt<1>("h01") 
    pcPlusFour.io.inputx <= pc 
    pcPlusFour.io.inputy <= UInt<3>("h04") 
    if_id.io.in.instruction <= io.imem.instruction 
    if_id.io.in.pc <= pc 
    node _T_16 = eq(hazard.io.if_id_stall, UInt<1>("h00")) 
    if_id.io.valid <= _T_16 
    if_id.io.flush <= hazard.io.if_id_flush 
    node _T_17 = bits(if_id.io.data.instruction, 6, 0) 
    control.io.opcode <= _T_17 
    node rs1 = bits(if_id.io.data.instruction, 19, 15) 
    node rs2 = bits(if_id.io.data.instruction, 24, 20) 
    hazard.io.rs1 <= rs1 
    hazard.io.rs2 <= rs2 
    registers.io.readreg1 <= rs1 
    registers.io.readreg2 <= rs2 
    registers.io.writedata <= write_data 
    immGen.io.instruction <= if_id.io.data.instruction 
    branchAdd.io.inputx <= if_id.io.data.pc 
    branchAdd.io.inputy <= immGen.io.sextImm 
    id_next_pc <= branchAdd.io.result 
    predictor.io.pc <= if_id.io.data.pc 
    node _T_18 = and(control.io.branch, predictor.io.prediction) 
    hazard.io.id_prediction <= _T_18 
    id_ex.io.in.pc <= if_id.io.data.pc 
    id_ex.io.in.sextImm <= immGen.io.sextImm 
    id_ex.io.in.instruction <= if_id.io.data.instruction 
    id_ex.io.in.readdata1 <= registers.io.readdata1 
    id_ex.io.in.readdata2 <= registers.io.readdata2 
    id_ex_ctrl.io.in.ex_ctrl.aluop <= control.io.aluop 
    id_ex_ctrl.io.in.ex_ctrl.itype <= control.io.itype 
    id_ex_ctrl.io.in.ex_ctrl.resultselect <= control.io.resultselect 
    id_ex_ctrl.io.in.ex_ctrl.xsrc <= control.io.xsrc 
    id_ex_ctrl.io.in.ex_ctrl.ysrc <= control.io.ysrc 
    id_ex_ctrl.io.in.ex_ctrl.plus4 <= control.io.plus4 
    id_ex_ctrl.io.in.ex_ctrl.branch <= control.io.branch 
    id_ex_ctrl.io.in.ex_ctrl.jal <= control.io.jal 
    id_ex_ctrl.io.in.ex_ctrl.jalr <= control.io.jalr 
    id_ex_ctrl.io.in.ex_ctrl.prediction <= predictor.io.prediction 
    id_ex_ctrl.io.in.mem_ctrl.memop <= control.io.memop 
    id_ex_ctrl.io.in.wb_ctrl.regwrite <= control.io.regwrite 
    id_ex_ctrl.io.in.wb_ctrl.toreg <= control.io.toreg 
    id_ex.io.valid <= UInt<1>("h01") 
    id_ex.io.flush <= hazard.io.id_ex_flush 
    id_ex_ctrl.io.valid <= UInt<1>("h01") 
    id_ex_ctrl.io.flush <= hazard.io.id_ex_flush 
    node _T_19 = bits(id_ex.io.data.instruction, 11, 7) 
    hazard.io.idex_rd <= _T_19 
    node _T_20 = eq(id_ex_ctrl.io.data.mem_ctrl.memop, UInt<2>("h02")) 
    when _T_20 : 
      hazard.io.idex_memread <= UInt<1>("h01") 
      skip 
    else : 
      hazard.io.idex_memread <= UInt<1>("h00") 
      skip 
    node _T_21 = bits(id_ex.io.data.instruction, 19, 15) 
    forwarding.io.rs1 <= _T_21 
    node _T_22 = bits(id_ex.io.data.instruction, 24, 20) 
    forwarding.io.rs2 <= _T_22 
    node _T_23 = bits(ex_mem.io.data.instruction, 11, 7) 
    forwarding.io.exmemrd <= _T_23 
    forwarding.io.exmemrw <= ex_mem_ctrl.io.data.wb_ctrl.regwrite 
    aluControl.io.itype <= id_ex_ctrl.io.data.ex_ctrl.itype 
    aluControl.io.aluop <= id_ex_ctrl.io.data.ex_ctrl.aluop 
    node _T_24 = bits(id_ex.io.data.instruction, 14, 12) 
    aluControl.io.funct3 <= _T_24 
    node _T_25 = bits(id_ex.io.data.instruction, 31, 25) 
    aluControl.io.funct7 <= _T_25 
    nextPCmod.io.branch <= id_ex_ctrl.io.data.ex_ctrl.branch 
    nextPCmod.io.jal <= id_ex_ctrl.io.data.ex_ctrl.jal 
    nextPCmod.io.jalr <= id_ex_ctrl.io.data.ex_ctrl.jalr 
    wire forward_a_mux : UInt<32> 
    node _T_26 = eq(forwarding.io.forwardA, UInt<1>("h00")) 
    when _T_26 : 
      forward_a_mux <= id_ex.io.data.readdata1 
      skip 
    else : 
      node _T_27 = eq(forwarding.io.forwardA, UInt<1>("h01")) 
      when _T_27 : 
        forward_a_mux <= ex_mem.io.data.ex_result 
        skip 
      else : 
        forward_a_mux <= write_data 
        skip 
    wire forward_b_mux : UInt<32> 
    node _T_28 = eq(forwarding.io.forwardB, UInt<1>("h00")) 
    when _T_28 : 
      forward_b_mux <= id_ex.io.data.readdata2 
      skip 
    else : 
      node _T_29 = eq(forwarding.io.forwardB, UInt<1>("h01")) 
      when _T_29 : 
        forward_b_mux <= ex_mem.io.data.ex_result 
        skip 
      else : 
        forward_b_mux <= write_data 
        skip 
    alu.io.operation <= aluControl.io.operation 
    node _T_30 = eq(id_ex_ctrl.io.data.ex_ctrl.xsrc, UInt<1>("h01")) 
    when _T_30 : 
      alu.io.inputx <= id_ex.io.data.pc 
      skip 
    else : 
      alu.io.inputx <= forward_a_mux 
      skip 
    node _T_31 = eq(id_ex_ctrl.io.data.ex_ctrl.plus4, UInt<1>("h01")) 
    when _T_31 : 
      alu.io.inputy <= UInt<3>("h04") 
      skip 
    else : 
      node _T_32 = eq(id_ex_ctrl.io.data.ex_ctrl.ysrc, UInt<1>("h01")) 
      when _T_32 : 
        alu.io.inputy <= id_ex.io.data.sextImm 
        skip 
      else : 
        alu.io.inputy <= forward_b_mux 
        skip 
      skip 
    nextPCmod.io.inputx <= forward_a_mux 
    nextPCmod.io.inputy <= forward_b_mux 
    node _T_33 = bits(id_ex.io.data.instruction, 14, 12) 
    nextPCmod.io.funct3 <= _T_33 
    nextPCmod.io.pc <= id_ex.io.data.pc 
    nextPCmod.io.imm <= id_ex.io.data.sextImm 
    ex_mem.io.in.instruction <= id_ex.io.data.instruction 
    ex_mem.io.in.mem_writedata <= forward_b_mux 
    ex_mem_ctrl.io.in.mem_ctrl.memop <= id_ex_ctrl.io.data.mem_ctrl.memop 
    ex_mem_ctrl.io.in.wb_ctrl.regwrite <= id_ex_ctrl.io.data.wb_ctrl.regwrite 
    ex_mem_ctrl.io.in.wb_ctrl.toreg <= id_ex_ctrl.io.data.wb_ctrl.toreg 
    ex_mem.io.in.next_pc <= nextPCmod.io.nextpc 
    ex_mem.io.in.taken <= nextPCmod.io.taken 
    node _T_34 = eq(id_ex_ctrl.io.data.ex_ctrl.resultselect, UInt<1>("h00")) 
    when _T_34 : 
      ex_mem.io.in.ex_result <= alu.io.result 
      skip 
    else : 
      ex_mem.io.in.ex_result <= id_ex.io.data.sextImm 
      skip 
    node _T_35 = not(hazard.io.ex_mem_flush) 
    node _T_36 = and(id_ex_ctrl.io.data.ex_ctrl.branch, _T_35) 
    when _T_36 : 
      predictor.io.update <= UInt<1>("h01") 
      predictor.io.taken <= nextPCmod.io.taken 
      node _T_37 = eq(id_ex_ctrl.io.data.ex_ctrl.prediction, nextPCmod.io.taken) 
      when _T_37 : 
        node _T_38 = add(bpCorrect, UInt<1>("h01")) 
        node _T_39 = tail(_T_38, 1) 
        bpCorrect <= _T_39 
        skip 
      else : 
        node _T_40 = add(bpIncorrect, UInt<1>("h01")) 
        node _T_41 = tail(_T_40, 1) 
        bpIncorrect <= _T_41 
        skip 
      skip 
    else : 
      predictor.io.update <= UInt<1>("h00") 
      predictor.io.taken is invalid 
      skip 
    when id_ex_ctrl.io.data.ex_ctrl.branch : 
      node _T_42 = neq(id_ex_ctrl.io.data.ex_ctrl.prediction, nextPCmod.io.taken) 
      when _T_42 : 
        ex_mem.io.in.taken <= UInt<1>("h01") 
        skip 
      else : 
        ex_mem.io.in.taken <= UInt<1>("h00") 
        skip 
      skip 
    ex_mem.io.valid <= UInt<1>("h01") 
    ex_mem.io.flush <= hazard.io.ex_mem_flush 
    ex_mem_ctrl.io.valid <= UInt<1>("h01") 
    ex_mem_ctrl.io.flush <= hazard.io.ex_mem_flush 
    io.dmem.address <= ex_mem.io.data.ex_result 
    node _T_43 = eq(ex_mem_ctrl.io.data.mem_ctrl.memop, UInt<2>("h02")) 
    io.dmem.memread <= _T_43 
    node _T_44 = eq(ex_mem_ctrl.io.data.mem_ctrl.memop, UInt<2>("h03")) 
    io.dmem.memwrite <= _T_44 
    node _T_45 = bits(ex_mem_ctrl.io.data.mem_ctrl.memop, 1, 1) 
    io.dmem.valid <= _T_45 
    node _T_46 = bits(ex_mem.io.data.instruction, 13, 12) 
    io.dmem.maskmode <= _T_46 
    node _T_47 = bits(ex_mem.io.data.instruction, 14, 14) 
    node _T_48 = not(_T_47) 
    io.dmem.sext <= _T_48 
    io.dmem.writedata <= ex_mem.io.data.mem_writedata 
    next_pc <= ex_mem.io.data.next_pc 
    hazard.io.exmem_taken <= ex_mem.io.data.taken 
    mem_wb.io.in.ex_result <= ex_mem.io.data.ex_result 
    mem_wb.io.in.instruction <= ex_mem.io.data.instruction 
    mem_wb.io.in.readdata <= io.dmem.readdata 
    mem_wb_ctrl.io.in.wb_ctrl.regwrite <= ex_mem_ctrl.io.data.wb_ctrl.regwrite 
    mem_wb_ctrl.io.in.wb_ctrl.toreg <= ex_mem_ctrl.io.data.wb_ctrl.toreg 
    mem_wb.io.valid <= UInt<1>("h01") 
    mem_wb.io.flush <= UInt<1>("h00") 
    mem_wb_ctrl.io.valid <= UInt<1>("h01") 
    mem_wb_ctrl.io.flush <= UInt<1>("h00") 
    node _T_49 = bits(mem_wb.io.data.instruction, 11, 7) 
    node _T_50 = eq(_T_49, UInt<1>("h00")) 
    when _T_50 : 
      registers.io.wen <= UInt<1>("h00") 
      skip 
    else : 
      registers.io.wen <= mem_wb_ctrl.io.data.wb_ctrl.regwrite 
      skip 
    node _T_51 = bits(mem_wb.io.data.instruction, 11, 7) 
    registers.io.writereg <= _T_51 
    node _T_52 = eq(mem_wb_ctrl.io.data.wb_ctrl.toreg, UInt<1>("h01")) 
    when _T_52 : 
      write_data <= mem_wb.io.data.readdata 
      skip 
    else : 
      write_data <= mem_wb.io.data.ex_result 
      skip 
    node _T_53 = bits(mem_wb.io.data.instruction, 11, 7) 
    forwarding.io.memwbrd <= _T_53 
    forwarding.io.memwbrw <= mem_wb_ctrl.io.data.wb_ctrl.regwrite 
    
