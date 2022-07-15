circuit StageReg :
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
    
