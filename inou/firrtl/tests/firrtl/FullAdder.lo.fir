circuit FullAdder :
  module FullAdder :
    input clock : Clock
    input reset : UInt<1>
    input io_a : UInt<1>
    input io_b : UInt<1>
    input io_cin : UInt<1>
    output io_sum : UInt<1>
    output io_cout : UInt<1>

    node a_xor_b = xor(io_a, io_b) @[FullAdder.scala 16:22]
    node _T = xor(a_xor_b, io_cin) @[FullAdder.scala 17:21]
    node a_and_b = and(io_a, io_b) @[FullAdder.scala 19:22]
    node b_and_cin = and(io_b, io_cin) @[FullAdder.scala 20:24]
    node a_and_cin = and(io_a, io_cin) @[FullAdder.scala 21:24]
    node _T_1 = or(a_and_b, b_and_cin) @[FullAdder.scala 22:22]
    node _T_2 = or(_T_1, a_and_cin) @[FullAdder.scala 22:34]
    io_sum <= _T @[FullAdder.scala 17:10]
    io_cout <= _T_2 @[FullAdder.scala 22:11]