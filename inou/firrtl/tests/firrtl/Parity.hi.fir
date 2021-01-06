circuit Parity :
  module Parity :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip in : UInt<1>, out : UInt<1>}

    reg state : UInt<1>, clock with :
      reset => (reset, UInt<1>("h0")) @[Parity.scala 13:23]
    when io.in : @[Parity.scala 14:16]
      node _T = eq(state, UInt<1>("h0")) @[Parity.scala 15:17]
      when _T : @[Parity.scala 15:29]
        state <= UInt<1>("h1") @[Parity.scala 15:37]
      else :
        state <= UInt<1>("h0") @[Parity.scala 16:37]
    node _T_1 = eq(state, UInt<1>("h1")) @[Parity.scala 18:20]
    io.out <= _T_1 @[Parity.scala 18:10]