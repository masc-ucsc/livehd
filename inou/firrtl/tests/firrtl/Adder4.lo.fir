circuit Adder4 :
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

  module Adder4 :
    input clock : Clock
    input reset : UInt<1>
    input io_A : UInt<4>
    input io_B : UInt<4>
    input io_Cin : UInt<1>
    output io_Sum : UInt<4>
    output io_Cout : UInt<1>

    inst Adder0 of FullAdder @[Adder4.scala 17:22]
    inst Adder1 of FullAdder @[Adder4.scala 23:22]
    inst Adder2 of FullAdder @[Adder4.scala 29:22]
    inst Adder3 of FullAdder @[Adder4.scala 35:22]
    node _T = bits(io_A, 0, 0) @[Adder4.scala 18:22]
    node _T_1 = bits(io_B, 0, 0) @[Adder4.scala 19:22]
    node _T_2 = bits(io_A, 1, 1) @[Adder4.scala 24:22]
    node _T_3 = bits(io_B, 1, 1) @[Adder4.scala 25:22]
    node s1 = cat(Adder1.io_sum, Adder0.io_sum) @[Cat.scala 29:58]
    node _T_4 = bits(io_A, 2, 2) @[Adder4.scala 30:22]
    node _T_5 = bits(io_B, 2, 2) @[Adder4.scala 31:22]
    node s2 = cat(Adder2.io_sum, s1) @[Cat.scala 29:58]
    node _T_6 = bits(io_A, 3, 3) @[Adder4.scala 36:22]
    node _T_7 = bits(io_B, 3, 3) @[Adder4.scala 37:22]
    node _T_8 = cat(Adder3.io_sum, s2) @[Cat.scala 29:58]
    io_Sum <= _T_8 @[Adder4.scala 39:10]
    io_Cout <= Adder3.io_cout @[Adder4.scala 40:11]
    Adder0.clock <= clock
    Adder0.reset <= reset
    Adder0.io_a <= _T @[Adder4.scala 18:15]
    Adder0.io_b <= _T_1 @[Adder4.scala 19:15]
    Adder0.io_cin <= io_Cin @[Adder4.scala 20:17]
    Adder1.clock <= clock
    Adder1.reset <= reset
    Adder1.io_a <= _T_2 @[Adder4.scala 24:15]
    Adder1.io_b <= _T_3 @[Adder4.scala 25:15]
    Adder1.io_cin <= Adder0.io_cout @[Adder4.scala 26:17]
    Adder2.clock <= clock
    Adder2.reset <= reset
    Adder2.io_a <= _T_4 @[Adder4.scala 30:15]
    Adder2.io_b <= _T_5 @[Adder4.scala 31:15]
    Adder2.io_cin <= Adder1.io_cout @[Adder4.scala 32:17]
    Adder3.clock <= clock
    Adder3.reset <= reset
    Adder3.io_a <= _T_6 @[Adder4.scala 36:15]
    Adder3.io_b <= _T_7 @[Adder4.scala 37:15]
    Adder3.io_cin <= Adder2.io_cout @[Adder4.scala 38:17]