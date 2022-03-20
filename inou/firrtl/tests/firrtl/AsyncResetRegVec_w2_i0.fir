circuit AsyncResetRegVec_w2_i0 :  
  module AsyncResetRegVec_w2_i0 : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip d : UInt<2>, q : UInt<2>, flip en : UInt<1>}
    
    clock is invalid
    reset is invalid
    io is invalid
    node _T = asAsyncReset(reset) @[AsyncResetReg.scala 64:29]
    reg reg : UInt<2>, clock with : (reset => (_T, UInt<2>("h00"))) @[AsyncResetReg.scala 64:50]
    when io.en : @[AsyncResetReg.scala 65:16]
      reg <= io.d @[AsyncResetReg.scala 66:9]
      skip @[AsyncResetReg.scala 65:16]
    io.q <= reg @[AsyncResetReg.scala 68:8]
