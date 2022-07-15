circuit StageReg_3 :
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
    
