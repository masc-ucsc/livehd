circuit Arbiter_simplified :
  module Arbiter_simplified : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip in : {valid : UInt<4>}[2], out : {valid : UInt<4>}[2]}
    io.out[0].valid <= io.in[0].valid
    io.out[1].valid <= io.in[1].valid
