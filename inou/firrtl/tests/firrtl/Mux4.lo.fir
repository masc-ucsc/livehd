circuit Mux4 :
  module Mux2 :
    input clock : Clock
    input reset : UInt<1>
    input io_sel : UInt<1>
    input io_in0 : UInt<1>
    input io_in1 : UInt<1>
    output io_out : UInt<1>

    node _T = and(io_sel, io_in1) @[Mux4.scala 18:21]
    node _T_1 = not(io_sel) @[Mux4.scala 18:34]
    node _T_2 = and(_T_1, io_in0) @[Mux4.scala 18:42]
    node _T_3 = or(_T, _T_2) @[Mux4.scala 18:31]
    io_out <= _T_3 @[Mux4.scala 18:10]

  module Mux4 :
    input clock : Clock
    input reset : UInt<1>
    input io_in0 : UInt<1>
    input io_in1 : UInt<1>
    input io_in2 : UInt<1>
    input io_in3 : UInt<1>
    input io_sel : UInt<2>
    output io_out : UInt<1>

    inst m0 of Mux2 @[Mux4.scala 36:18]
    inst m1 of Mux2 @[Mux4.scala 41:18]
    inst m2 of Mux2 @[Mux4.scala 46:18]
    node _T = bits(io_sel, 0, 0) @[Mux4.scala 37:22]
    node _T_1 = bits(io_sel, 0, 0) @[Mux4.scala 42:22]
    node _T_2 = bits(io_sel, 1, 1) @[Mux4.scala 47:22]
    io_out <= m2.io_out @[Mux4.scala 51:10]
    m0.clock <= clock
    m0.reset <= reset
    m0.io_sel <= _T @[Mux4.scala 37:13]
    m0.io_in0 <= io_in0 @[Mux4.scala 38:13]
    m0.io_in1 <= io_in1 @[Mux4.scala 39:13]
    m1.clock <= clock
    m1.reset <= reset
    m1.io_sel <= _T_1 @[Mux4.scala 42:13]
    m1.io_in0 <= io_in2 @[Mux4.scala 43:13]
    m1.io_in1 <= io_in3 @[Mux4.scala 44:13]
    m2.clock <= clock
    m2.reset <= reset
    m2.io_sel <= _T_2 @[Mux4.scala 47:13]
    m2.io_in0 <= m0.io_out @[Mux4.scala 48:13]
    m2.io_in1 <= m1.io_out @[Mux4.scala 49:13]