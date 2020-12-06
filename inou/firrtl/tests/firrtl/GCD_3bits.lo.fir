circuit GCD_3bits :
  module GCD_3bits :
    input clock : Clock
    input reset : UInt<1>
    input io_value1 : UInt<3>
    input io_value2 : UInt<3>
    input io_loadingValues : UInt<1>
    output io_outputGCD : UInt<3>
    output io_outputValid : UInt<1>

    reg x : UInt<3>, clock with :
      reset => (UInt<1>("h0"), x) @[GCD_3bits.scala 21:15]
    reg y : UInt<3>, clock with :
      reset => (UInt<1>("h0"), y) @[GCD_3bits.scala 22:15]
    node _T = gt(x, y) @[GCD_3bits.scala 24:10]
    node _x_T = sub(x, y) @[GCD_3bits.scala 24:24]
    node _x_T_1 = tail(_x_T, 1) @[GCD_3bits.scala 24:24]
    node _y_T = sub(y, x) @[GCD_3bits.scala 25:25]
    node _y_T_1 = tail(_y_T, 1) @[GCD_3bits.scala 25:25]
    node _GEN_0 = mux(_T, _x_T_1, x) @[GCD_3bits.scala 24:15 GCD_3bits.scala 24:19 GCD_3bits.scala 21:15]
    node _GEN_1 = mux(_T, y, _y_T_1) @[GCD_3bits.scala 24:15 GCD_3bits.scala 22:15 GCD_3bits.scala 25:20]
    node _GEN_2 = mux(io_loadingValues, io_value1, _GEN_0) @[GCD_3bits.scala 27:26 GCD_3bits.scala 28:7]
    node _GEN_3 = mux(io_loadingValues, io_value2, _GEN_1) @[GCD_3bits.scala 27:26 GCD_3bits.scala 29:7]
    node _io_outputValid_T = eq(y, UInt<1>("h0")) @[GCD_3bits.scala 33:23]
    io_outputGCD <= x @[GCD_3bits.scala 32:16]
    io_outputValid <= _io_outputValid_T @[GCD_3bits.scala 33:18]
    x <= _GEN_2
    y <= _GEN_3