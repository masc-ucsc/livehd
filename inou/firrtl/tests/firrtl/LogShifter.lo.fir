circuit LogShifter :
  module LogShifter :
    input clock : Clock
    input reset : UInt<1>
    input io_in : UInt<16>
    input io_shamt : UInt<4>
    output io_out : UInt<16>

    reg s0 : UInt<16>, clock with :
      reset => (UInt<1>("h0"), s0) @[LogShifter.scala 12:19]
    node _T = bits(io_shamt, 3, 3) @[LogShifter.scala 13:17]
    node _T_1 = eq(_T, UInt<1>("h1")) @[LogShifter.scala 13:21]
    node _T_2 = dshl(io_in, UInt<4>("h8")) @[LogShifter.scala 14:17]
    node _GEN_0 = mux(_T_1, _T_2, io_in) @[LogShifter.scala 13:30 LogShifter.scala 14:8 LogShifter.scala 16:8]
    reg s1 : UInt<16>, clock with :
      reset => (UInt<1>("h0"), s1) @[LogShifter.scala 18:19]
    node _T_3 = bits(io_shamt, 2, 2) @[LogShifter.scala 19:17]
    node _T_4 = eq(_T_3, UInt<1>("h1")) @[LogShifter.scala 19:21]
    node _T_5 = dshl(s0, UInt<3>("h4")) @[LogShifter.scala 20:14]
    node _GEN_1 = mux(_T_4, _T_5, s0) @[LogShifter.scala 19:30 LogShifter.scala 20:8 LogShifter.scala 22:8]
    reg s2 : UInt<16>, clock with :
      reset => (UInt<1>("h0"), s2) @[LogShifter.scala 24:19]
    node _T_6 = bits(io_shamt, 1, 1) @[LogShifter.scala 25:17]
    node _T_7 = eq(_T_6, UInt<1>("h1")) @[LogShifter.scala 25:21]
    node _T_8 = dshl(s1, UInt<2>("h2")) @[LogShifter.scala 26:14]
    node _GEN_2 = mux(_T_7, _T_8, s1) @[LogShifter.scala 25:30 LogShifter.scala 26:8 LogShifter.scala 28:8]
    node _T_9 = bits(io_shamt, 1, 1) @[LogShifter.scala 30:17]
    node _T_10 = eq(_T_9, UInt<1>("h1")) @[LogShifter.scala 30:21]
    node _T_11 = dshl(s2, UInt<1>("h1")) @[LogShifter.scala 31:18]
    node _GEN_3 = mux(_T_10, _T_11, s2) @[LogShifter.scala 30:30 LogShifter.scala 31:12 LogShifter.scala 33:12]
    io_out <= bits(_GEN_3, 15, 0)
    s0 <= mux(reset, UInt<16>("h0"), bits(_GEN_0, 15, 0)) @[LogShifter.scala 12:19 LogShifter.scala 12:19]
    s1 <= mux(reset, UInt<16>("h0"), bits(_GEN_1, 15, 0)) @[LogShifter.scala 18:19 LogShifter.scala 18:19]
    s2 <= mux(reset, UInt<16>("h0"), bits(_GEN_2, 15, 0)) @[LogShifter.scala 24:19 LogShifter.scala 24:19]