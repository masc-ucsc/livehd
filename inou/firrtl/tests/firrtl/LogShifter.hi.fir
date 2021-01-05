circuit LogShifter :
  module LogShifter :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip in : UInt<16>, flip shamt : UInt<4>, out : UInt<16>}

    reg s0 : UInt<16>, clock with :
      reset => (reset, UInt<16>("h0")) @[LogShifter.scala 12:19]
    node _T = bits(io.shamt, 3, 3) @[LogShifter.scala 13:17]
    node _T_1 = eq(_T, UInt<1>("h1")) @[LogShifter.scala 13:21]
    when _T_1 : @[LogShifter.scala 13:30]
      node _T_2 = dshl(io.in, UInt<4>("h8")) @[LogShifter.scala 14:17]
      s0 <= _T_2 @[LogShifter.scala 14:8]
    else :
      s0 <= io.in @[LogShifter.scala 16:8]
    reg s1 : UInt<16>, clock with :
      reset => (reset, UInt<16>("h0")) @[LogShifter.scala 18:19]
    node _T_3 = bits(io.shamt, 2, 2) @[LogShifter.scala 19:17]
    node _T_4 = eq(_T_3, UInt<1>("h1")) @[LogShifter.scala 19:21]
    when _T_4 : @[LogShifter.scala 19:30]
      node _T_5 = dshl(s0, UInt<3>("h4")) @[LogShifter.scala 20:14]
      s1 <= _T_5 @[LogShifter.scala 20:8]
    else :
      s1 <= s0 @[LogShifter.scala 22:8]
    reg s2 : UInt<16>, clock with :
      reset => (reset, UInt<16>("h0")) @[LogShifter.scala 24:19]
    node _T_6 = bits(io.shamt, 1, 1) @[LogShifter.scala 25:17]
    node _T_7 = eq(_T_6, UInt<1>("h1")) @[LogShifter.scala 25:21]
    when _T_7 : @[LogShifter.scala 25:30]
      node _T_8 = dshl(s1, UInt<2>("h2")) @[LogShifter.scala 26:14]
      s2 <= _T_8 @[LogShifter.scala 26:8]
    else :
      s2 <= s1 @[LogShifter.scala 28:8]
    node _T_9 = bits(io.shamt, 1, 1) @[LogShifter.scala 30:17]
    node _T_10 = eq(_T_9, UInt<1>("h1")) @[LogShifter.scala 30:21]
    when _T_10 : @[LogShifter.scala 30:30]
      node _T_11 = dshl(s2, UInt<1>("h1")) @[LogShifter.scala 31:18]
      io.out <= _T_11 @[LogShifter.scala 31:12]
    else :
      io.out <= s2 @[LogShifter.scala 33:12]