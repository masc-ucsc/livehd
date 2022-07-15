circuit StageReg_5 :
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
    
