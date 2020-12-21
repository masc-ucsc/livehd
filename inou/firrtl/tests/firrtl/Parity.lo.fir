circuit Parity :
  module Parity :
    input clock : Clock
    input reset : UInt<1>
    input io_in : UInt<1>
    output io_out : UInt<1>

    reg state : UInt<1>, clock with :
      reset => (UInt<1>("h0"), state) @[Parity.scala 13:23]
    node _T = eq(state, UInt<1>("h0")) @[Parity.scala 15:17]
    node _GEN_0 = mux(_T, UInt<1>("h1"), UInt<1>("h0")) @[Parity.scala 15:29 Parity.scala 15:37 Parity.scala 16:37]
    node _GEN_1 = mux(io_in, _GEN_0, state) @[Parity.scala 14:16 Parity.scala 13:23]
    node _T_1 = eq(state, UInt<1>("h1")) @[Parity.scala 18:20]
    io_out <= _T_1 @[Parity.scala 18:10]
    state <= mux(reset, UInt<1>("h0"), _GEN_1) @[Parity.scala 13:23 Parity.scala 13:23]