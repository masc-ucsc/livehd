circuit Adder :
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

  module Adder :
    input clock : Clock
    input reset : UInt<1>
    input io_A : UInt<8>
    input io_B : UInt<8>
    input io_Cin : UInt<1>
    output io_Sum : UInt<8>
    output io_Cout : UInt<1>

    inst FullAdder of FullAdder @[Adder.scala 19:35]
    inst FullAdder_1 of FullAdder @[Adder.scala 19:35]
    inst FullAdder_2 of FullAdder @[Adder.scala 19:35]
    inst FullAdder_3 of FullAdder @[Adder.scala 19:35]
    inst FullAdder_4 of FullAdder @[Adder.scala 19:35]
    inst FullAdder_5 of FullAdder @[Adder.scala 19:35]
    inst FullAdder_6 of FullAdder @[Adder.scala 19:35]
    inst FullAdder_7 of FullAdder @[Adder.scala 19:35]
    node _T = bits(io_A, 0, 0) @[Adder.scala 28:21]
    node _T_1 = bits(io_B, 0, 0) @[Adder.scala 29:21]
    node _T_2 = bits(FullAdder.io_sum, 0, 0) @[Adder.scala 32:26]
    node _T_3 = bits(io_A, 1, 1) @[Adder.scala 28:21]
    node _T_4 = bits(io_B, 1, 1) @[Adder.scala 29:21]
    node _T_5 = bits(FullAdder_1.io_sum, 0, 0) @[Adder.scala 32:26]
    node _T_6 = bits(io_A, 2, 2) @[Adder.scala 28:21]
    node _T_7 = bits(io_B, 2, 2) @[Adder.scala 29:21]
    node _T_8 = bits(FullAdder_2.io_sum, 0, 0) @[Adder.scala 32:26]
    node _T_9 = bits(io_A, 3, 3) @[Adder.scala 28:21]
    node _T_10 = bits(io_B, 3, 3) @[Adder.scala 29:21]
    node _T_11 = bits(FullAdder_3.io_sum, 0, 0) @[Adder.scala 32:26]
    node _T_12 = bits(io_A, 4, 4) @[Adder.scala 28:21]
    node _T_13 = bits(io_B, 4, 4) @[Adder.scala 29:21]
    node _T_14 = bits(FullAdder_4.io_sum, 0, 0) @[Adder.scala 32:26]
    node _T_15 = bits(io_A, 5, 5) @[Adder.scala 28:21]
    node _T_16 = bits(io_B, 5, 5) @[Adder.scala 29:21]
    node _T_17 = bits(FullAdder_5.io_sum, 0, 0) @[Adder.scala 32:26]
    node _T_18 = bits(io_A, 6, 6) @[Adder.scala 28:21]
    node _T_19 = bits(io_B, 6, 6) @[Adder.scala 29:21]
    node _T_20 = bits(FullAdder_6.io_sum, 0, 0) @[Adder.scala 32:26]
    node _T_21 = bits(io_A, 7, 7) @[Adder.scala 28:21]
    node _T_22 = bits(io_B, 7, 7) @[Adder.scala 29:21]
    node _T_23 = bits(FullAdder_7.io_sum, 0, 0) @[Adder.scala 32:26]
    node sum_1 = _T_5 @[Adder.scala 21:19 Adder.scala 32:12]
    node sum_0 = _T_2 @[Adder.scala 21:19 Adder.scala 32:12]
    node _T_24 = cat(sum_1, sum_0) @[Adder.scala 34:17]
    node sum_3 = _T_11 @[Adder.scala 21:19 Adder.scala 32:12]
    node sum_2 = _T_8 @[Adder.scala 21:19 Adder.scala 32:12]
    node _T_25 = cat(sum_3, sum_2) @[Adder.scala 34:17]
    node _T_26 = cat(_T_25, _T_24) @[Adder.scala 34:17]
    node sum_5 = _T_17 @[Adder.scala 21:19 Adder.scala 32:12]
    node sum_4 = _T_14 @[Adder.scala 21:19 Adder.scala 32:12]
    node _T_27 = cat(sum_5, sum_4) @[Adder.scala 34:17]
    node sum_7 = _T_23 @[Adder.scala 21:19 Adder.scala 32:12]
    node sum_6 = _T_20 @[Adder.scala 21:19 Adder.scala 32:12]
    node _T_28 = cat(sum_7, sum_6) @[Adder.scala 34:17]
    node _T_29 = cat(_T_28, _T_27) @[Adder.scala 34:17]
    node _T_30 = cat(_T_29, _T_26) @[Adder.scala 34:17]
    node carry_0 = io_Cin @[Adder.scala 20:19 Adder.scala 24:12]
    node carry_1 = FullAdder.io_cout @[Adder.scala 20:19 Adder.scala 31:16]
    node carry_2 = FullAdder_1.io_cout @[Adder.scala 20:19 Adder.scala 31:16]
    node carry_3 = FullAdder_2.io_cout @[Adder.scala 20:19 Adder.scala 31:16]
    node carry_4 = FullAdder_3.io_cout @[Adder.scala 20:19 Adder.scala 31:16]
    node carry_5 = FullAdder_4.io_cout @[Adder.scala 20:19 Adder.scala 31:16]
    node carry_6 = FullAdder_5.io_cout @[Adder.scala 20:19 Adder.scala 31:16]
    node carry_7 = FullAdder_6.io_cout @[Adder.scala 20:19 Adder.scala 31:16]
    node carry_8 = FullAdder_7.io_cout @[Adder.scala 20:19 Adder.scala 31:16]
    io_Sum <= _T_30 @[Adder.scala 34:10]
    io_Cout <= carry_8 @[Adder.scala 35:11]
    FullAdder.clock <= clock
    FullAdder.reset <= reset
    FullAdder.io_a <= _T @[Adder.scala 28:14]
    FullAdder.io_b <= _T_1 @[Adder.scala 29:14]
    FullAdder.io_cin <= carry_0 @[Adder.scala 30:16]
    FullAdder_1.clock <= clock
    FullAdder_1.reset <= reset
    FullAdder_1.io_a <= _T_3 @[Adder.scala 28:14]
    FullAdder_1.io_b <= _T_4 @[Adder.scala 29:14]
    FullAdder_1.io_cin <= carry_1 @[Adder.scala 30:16]
    FullAdder_2.clock <= clock
    FullAdder_2.reset <= reset
    FullAdder_2.io_a <= _T_6 @[Adder.scala 28:14]
    FullAdder_2.io_b <= _T_7 @[Adder.scala 29:14]
    FullAdder_2.io_cin <= carry_2 @[Adder.scala 30:16]
    FullAdder_3.clock <= clock
    FullAdder_3.reset <= reset
    FullAdder_3.io_a <= _T_9 @[Adder.scala 28:14]
    FullAdder_3.io_b <= _T_10 @[Adder.scala 29:14]
    FullAdder_3.io_cin <= carry_3 @[Adder.scala 30:16]
    FullAdder_4.clock <= clock
    FullAdder_4.reset <= reset
    FullAdder_4.io_a <= _T_12 @[Adder.scala 28:14]
    FullAdder_4.io_b <= _T_13 @[Adder.scala 29:14]
    FullAdder_4.io_cin <= carry_4 @[Adder.scala 30:16]
    FullAdder_5.clock <= clock
    FullAdder_5.reset <= reset
    FullAdder_5.io_a <= _T_15 @[Adder.scala 28:14]
    FullAdder_5.io_b <= _T_16 @[Adder.scala 29:14]
    FullAdder_5.io_cin <= carry_5 @[Adder.scala 30:16]
    FullAdder_6.clock <= clock
    FullAdder_6.reset <= reset
    FullAdder_6.io_a <= _T_18 @[Adder.scala 28:14]
    FullAdder_6.io_b <= _T_19 @[Adder.scala 29:14]
    FullAdder_6.io_cin <= carry_6 @[Adder.scala 30:16]
    FullAdder_7.clock <= clock
    FullAdder_7.reset <= reset
    FullAdder_7.io_a <= _T_21 @[Adder.scala 28:14]
    FullAdder_7.io_b <= _T_22 @[Adder.scala 29:14]
    FullAdder_7.io_cin <= carry_7 @[Adder.scala 30:16]