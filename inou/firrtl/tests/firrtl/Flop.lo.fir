circuit Flop :
  module Flop :
    input clock : Clock
    input reset : UInt<1>
    input io_inp : UInt<16>
    input io_loading : UInt<1>
    output io_out : UInt<16>

    reg x : UInt<16>, clock with :
      reset => (UInt<1>("h0"), x) @[Flop.scala 14:15]
    node _GEN_0 = mux(io_loading, io_inp, x) @[Flop.scala 16:20 Flop.scala 17:7 Flop.scala 14:15]
    io_out <= x @[Flop.scala 20:10]
    x <= _GEN_0