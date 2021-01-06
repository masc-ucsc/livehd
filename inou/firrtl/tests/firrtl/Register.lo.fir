circuit Register :
  module Register :
    input clock : Clock
    input reset : UInt<1>
    input io_inVal : UInt<16>
    input io_loadingValues : UInt<1>
    output io_outVal : UInt<16>

    reg x : UInt<16>, clock with :
      reset => (UInt<1>("h0"), x) @[Register.scala 19:15]
    node _T = gt(x, UInt<1>("h0")) @[Register.scala 24:13]
    node _x_T = sub(x, UInt<1>("h1")) @[Register.scala 25:14]
    node _x_T_1 = tail(_x_T, 1) @[Register.scala 25:14]
    node _GEN_0 = mux(_T, _x_T_1, x) @[Register.scala 24:20 Register.scala 25:9 Register.scala 19:15]
    node _GEN_1 = mux(io_loadingValues, io_inVal, _GEN_0) @[Register.scala 21:27 Register.scala 22:7]
    io_outVal <= x @[Register.scala 29:13]
    x <= _GEN_1