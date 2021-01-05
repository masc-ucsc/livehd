circuit Flop :
  module Flop :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip inp : UInt<16>, flip loading : UInt<1>, out : UInt<16>}

    reg x : UInt<16>, clock with :
      reset => (UInt<1>("h0"), x) @[Flop.scala 14:15]
    when io.loading : @[Flop.scala 16:20]
      x <= io.inp @[Flop.scala 17:7]
    io.out <= x @[Flop.scala 20:10]