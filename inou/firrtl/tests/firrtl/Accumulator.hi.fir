circuit Accumulator :
  module Accumulator :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip in : UInt<1>, out : UInt<8>}

    reg accumulator : UInt<8>, clock with :
      reset => (reset, UInt<8>("h0")) @[Accumulator.scala 16:28]
    node _accumulator_T = add(accumulator, io.in) @[Accumulator.scala 17:30]
    node _accumulator_T_1 = tail(_accumulator_T, 1) @[Accumulator.scala 17:30]
    accumulator <= _accumulator_T_1 @[Accumulator.scala 17:15]
    io.out <= accumulator @[Accumulator.scala 18:10]