circuit RegisterSimple :
  module RegisterSimple :
    input clock : Clock
    input reset : UInt<1>
    input io_inVal : UInt<16>
    output io_outVal : UInt<16>

    reg x : UInt<16>, clock with :
      reset => (UInt<1>("h0"), x) @[RegisterSimple.scala 18:15]
    node _T = eq(x, UInt<1>("h0")) @[RegisterSimple.scala 20:11]
    node _x_T = sub(x, UInt<1>("h1")) @[RegisterSimple.scala 23:12]
    node _x_T_1 = tail(_x_T, 1) @[RegisterSimple.scala 23:12]
    node _GEN_0 = mux(_T, io_inVal, _x_T_1) @[RegisterSimple.scala 20:20 RegisterSimple.scala 21:7 RegisterSimple.scala 23:7]
    io_outVal <= x @[RegisterSimple.scala 26:13]
    x <= _GEN_0