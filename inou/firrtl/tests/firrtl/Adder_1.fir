circuit Adder_1 :
  module Adder_1 : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip inputx : UInt<32>, flip inputy : UInt<32>, result : UInt<32>}
    
    node _T = add(io.inputx, io.inputy) 
    node _T_1 = tail(_T, 1) 
    io.result <= _T_1 
    
