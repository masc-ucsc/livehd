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
    node _s0_T = dshl(io_in, UInt<4>("h8")) @[LogShifter.scala 14:17]
    node _GEN_0 = mux(_T_1, _s0_T, io_in) @[LogShifter.scala 13:30 LogShifter.scala 14:8 LogShifter.scala 16:8]
    node _T_2 = bits(io_shamt, 1, 1) @[LogShifter.scala 35:17]
    node _T_3 = eq(_T_2, UInt<1>("h1")) @[LogShifter.scala 35:21]
    node _io_out_T = dshl(s0, UInt<1>("h1")) @[LogShifter.scala 36:18]
    node _GEN_1 = mux(_T_3, _io_out_T, s0) @[LogShifter.scala 35:30 LogShifter.scala 36:12 LogShifter.scala 38:12]
    io_out <= bits(_GEN_1, 15, 0)
    s0 <= mux(reset, UInt<16>("h0"), bits(_GEN_0, 15, 0)) @[LogShifter.scala 12:19 LogShifter.scala 12:19]