circuit GCD_3bits :
  module GCD_3bits :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip value1 : UInt<3>, flip value2 : UInt<3>, flip loadingValues : UInt<1>, outputGCD : UInt<3>, outputValid : UInt<1>}

    reg x : UInt<3>, clock with :
      reset => (UInt<1>("h0"), x) @[GCD_3bits.scala 21:15]
    reg y : UInt<3>, clock with :
      reset => (UInt<1>("h0"), y) @[GCD_3bits.scala 22:15]
    node _T = gt(x, y) @[GCD_3bits.scala 24:10]
    when _T : @[GCD_3bits.scala 24:15]
      node _x_T = sub(x, y) @[GCD_3bits.scala 24:24]
      node _x_T_1 = tail(_x_T, 1) @[GCD_3bits.scala 24:24]
      x <= _x_T_1 @[GCD_3bits.scala 24:19]
    else :
      node _y_T = sub(y, x) @[GCD_3bits.scala 25:25]
      node _y_T_1 = tail(_y_T, 1) @[GCD_3bits.scala 25:25]
      y <= _y_T_1 @[GCD_3bits.scala 25:20]
    when io.loadingValues : @[GCD_3bits.scala 27:26]
      x <= io.value1 @[GCD_3bits.scala 28:7]
      y <= io.value2 @[GCD_3bits.scala 29:7]
    io.outputGCD <= x @[GCD_3bits.scala 32:16]
    node _io_outputValid_T = eq(y, UInt<1>("h0")) @[GCD_3bits.scala 33:23]
    io.outputValid <= _io_outputValid_T @[GCD_3bits.scala 33:18]