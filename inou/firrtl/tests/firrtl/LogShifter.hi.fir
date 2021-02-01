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
      node _s0_T = dshl(io.in, UInt<4>("h8")) //31b @[LogShifter.scala 14:17]
      s0 <= _s0_T @[LogShifter.scala 14:8]
    else :
      s0 <= io.in @[LogShifter.scala 16:8]
    node _T_2 = bits(io.shamt, 1, 1) @[LogShifter.scala 35:17]
    node _T_3 = eq(_T_2, UInt<1>("h1")) @[LogShifter.scala 35:21]


    when _T_3 : @[LogShifter.scala 35:30]
      node _io_out_T = dshl(s0, UInt<1>("h1")) @[LogShifter.scala 36:18]
      io.out <= _io_out_T @[LogShifter.scala 36:12]
    else :
      io.out <= s0 @[LogShifter.scala 38:12]