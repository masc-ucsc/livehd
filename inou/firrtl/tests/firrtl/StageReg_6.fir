circuit StageReg_6 :
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
    
