circuit StageReg_2 :
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
    
