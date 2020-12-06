circuit Register :
  module Register :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip inVal : UInt<16>, flip loadingValues : UInt<1>, outVal : UInt<16>}

    reg x : UInt, clock with :
      reset => (UInt<1>("h0"), x) @[Register.scala 19:15]
    when io.loadingValues : @[Register.scala 21:27]
      x <= io.inVal @[Register.scala 22:7]
      skip
    else :
      node _T = gt(x, UInt<1>("h0")) @[Register.scala 24:13]
      when _T : @[Register.scala 24:20]
        node _T_1 = sub(x, UInt<1>("h1")) @[Register.scala 25:14]
        node _T_2 = tail(_T_1, 1) @[Register.scala 25:14]
        x <= _T_2 @[Register.scala 25:9]
        skip
      skip
    io.outVal <= x @[Register.scala 29:13]