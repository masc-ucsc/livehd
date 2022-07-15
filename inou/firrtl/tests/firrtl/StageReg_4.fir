circuit StageReg_4 :
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
    
