circuit GCD :
  module GCD :
    input clock : Clock
    input reset : UInt<1>
    input io_value1 : UInt<16>
    input io_value2 : UInt<16>
    input io_loadingValues : UInt<1>
    output io_outputGCD : UInt<16>
    output io_outputValid : UInt<1>

    reg x : UInt<16>, clock with :
      reset => (UInt<1>("h0"), x) @[GCD.scala 21:15]
    reg y : UInt<16>, clock with :
      reset => (UInt<1>("h0"), y) @[GCD.scala 22:15]
    node _T = gt(x, y) @[GCD.scala 24:10]
    node _T_1 = sub(x, y) @[GCD.scala 24:24]
    node _T_2 = tail(_T_1, 1) @[GCD.scala 24:24]
    node _T_3 = sub(y, x) @[GCD.scala 25:25]
    node _T_4 = tail(_T_3, 1) @[GCD.scala 25:25]
    node _GEN_0 = mux(_T, _T_2, x) @[GCD.scala 24:15 GCD.scala 24:19 GCD.scala 21:15]
    node _GEN_1 = mux(_T, y, _T_4) @[GCD.scala 24:15 GCD.scala 22:15 GCD.scala 25:20]
    node _GEN_2 = mux(io_loadingValues, io_value1, _GEN_0) @[GCD.scala 27:26 GCD.scala 28:7]
    node _GEN_3 = mux(io_loadingValues, io_value2, _GEN_1) @[GCD.scala 27:26 GCD.scala 29:7]
    node _T_5 = eq(y, UInt<1>("h0")) @[GCD.scala 33:23]
    io_outputGCD <= x @[GCD.scala 32:16]
    io_outputValid <= _T_5 @[GCD.scala 33:18]
    x <= _GEN_2
    y <= _GEN_3