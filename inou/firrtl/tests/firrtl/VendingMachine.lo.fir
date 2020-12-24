circuit VendingMachine :
  module VendingMachine :
    input clock : Clock
    input reset : UInt<1>
    input io_nickel : UInt<1>
    input io_dime : UInt<1>
    output io_valid : UInt<1>

    reg state : UInt<3>, clock with :
      reset => (UInt<1>("h0"), state) @[VendingMachine.scala 22:22]
    node _T = eq(state, UInt<3>("h0")) @[VendingMachine.scala 23:15]
    node _GEN_0 = mux(io_nickel, UInt<3>("h1"), state) @[VendingMachine.scala 24:22 VendingMachine.scala 24:30 VendingMachine.scala 22:22]
    node _GEN_1 = mux(io_dime, UInt<3>("h2"), _GEN_0) @[VendingMachine.scala 25:22 VendingMachine.scala 25:30]
    node _GEN_2 = mux(_T, _GEN_1, state) @[VendingMachine.scala 23:26 VendingMachine.scala 22:22]
    node _T_1 = eq(state, UInt<3>("h1")) @[VendingMachine.scala 27:15]
    node _GEN_3 = mux(io_nickel, UInt<3>("h2"), _GEN_2) @[VendingMachine.scala 28:22 VendingMachine.scala 28:30]
    node _GEN_4 = mux(io_dime, UInt<3>("h3"), _GEN_3) @[VendingMachine.scala 29:22 VendingMachine.scala 29:30]
    node _GEN_5 = mux(_T_1, _GEN_4, _GEN_2) @[VendingMachine.scala 27:23]
    node _T_2 = eq(state, UInt<3>("h2")) @[VendingMachine.scala 31:15]
    node _GEN_6 = mux(io_nickel, UInt<3>("h3"), _GEN_5) @[VendingMachine.scala 32:22 VendingMachine.scala 32:30]
    node _GEN_7 = mux(io_dime, UInt<3>("h4"), _GEN_6) @[VendingMachine.scala 33:22 VendingMachine.scala 33:30]
    node _GEN_8 = mux(_T_2, _GEN_7, _GEN_5) @[VendingMachine.scala 31:24]
    node _T_3 = eq(state, UInt<3>("h3")) @[VendingMachine.scala 35:15]
    node _GEN_9 = mux(io_nickel, UInt<3>("h4"), _GEN_8) @[VendingMachine.scala 36:22 VendingMachine.scala 36:30]
    node _GEN_10 = mux(io_dime, UInt<3>("h4"), _GEN_9) @[VendingMachine.scala 37:22 VendingMachine.scala 37:30]
    node _GEN_11 = mux(_T_3, _GEN_10, _GEN_8) @[VendingMachine.scala 35:24]
    node _T_4 = eq(state, UInt<3>("h4")) @[VendingMachine.scala 39:15]
    node _GEN_12 = mux(_T_4, UInt<3>("h0"), _GEN_11) @[VendingMachine.scala 39:24 VendingMachine.scala 40:11]
    node _T_5 = eq(state, UInt<3>("h4")) @[VendingMachine.scala 42:22]
    io_valid <= _T_5 @[VendingMachine.scala 42:12]
    state <= mux(reset, UInt<3>("h0"), _GEN_12) @[VendingMachine.scala 22:22 VendingMachine.scala 22:22]