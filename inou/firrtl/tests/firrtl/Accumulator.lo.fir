circuit Accumulator :
  module Accumulator :
    input clock : Clock
    input reset : UInt<1>
    input io_in : UInt<1>
    output io_out : UInt<8>

    reg accumulator : UInt<8>, clock with :
      reset => (UInt<1>("h0"), accumulator) @[Accumulator.scala 16:28]
    node _accumulator_T = add(accumulator, io_in) @[Accumulator.scala 17:30]
    node _accumulator_T_1 = tail(_accumulator_T, 1) @[Accumulator.scala 17:30]
    io_out <= accumulator @[Accumulator.scala 18:10]
    accumulator <= mux(reset, UInt<8>("h0"), _accumulator_T_1) @[Accumulator.scala 16:28 Accumulator.scala 16:28 Accumulator.scala 17:15]