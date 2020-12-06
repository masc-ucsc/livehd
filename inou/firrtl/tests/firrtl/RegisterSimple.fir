circuit RegisterSimple :
  module RegisterSimple :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip inVal : UInt<16>, outVal : UInt<16>}

    reg x : UInt, clock with :
      reset => (UInt<1>("h0"), x) @[RegisterSimple.scala 18:15]
    node _T = eq(x, UInt<1>("h0")) @[RegisterSimple.scala 20:11]
    when _T : @[RegisterSimple.scala 20:20]
      x <= io.inVal @[RegisterSimple.scala 21:7]
      skip
    else :
      node _T_1 = sub(x, UInt<1>("h1")) @[RegisterSimple.scala 23:12]
      node _T_2 = tail(_T_1, 1) @[RegisterSimple.scala 23:12]
      x <= _T_2 @[RegisterSimple.scala 23:7]
      skip
    io.outVal <= x @[RegisterSimple.scala 26:13]