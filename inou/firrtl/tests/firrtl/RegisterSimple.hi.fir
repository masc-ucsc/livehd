circuit RegisterSimple :
  module RegisterSimple :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip inVal : UInt<16>, outVal : UInt<16>}

    reg x : UInt<16>, clock with :
      reset => (UInt<1>("h0"), x) @[RegisterSimple.scala 18:15]
    node _T = eq(x, UInt<1>("h0")) @[RegisterSimple.scala 20:11]
    when _T : @[RegisterSimple.scala 20:20]
      x <= io.inVal @[RegisterSimple.scala 21:7]
    else :
      node _x_T = sub(x, UInt<1>("h1")) @[RegisterSimple.scala 23:12]
      node _x_T_1 = tail(_x_T, 1) @[RegisterSimple.scala 23:12]
      x <= _x_T_1 @[RegisterSimple.scala 23:7]
    io.outVal <= x @[RegisterSimple.scala 26:13]