circuit StageReg_1 :
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
    
