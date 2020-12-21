circuit VendingMachineSwitch :
  module VendingMachineSwitch :
    input clock : Clock
    input reset : UInt<1>
    input io_nickel : UInt<1>
    input io_dime : UInt<1>
    output io_valid : UInt<1>

    reg state : UInt<3>, clock with :
      reset => (UInt<1>("h0"), state) @[VendingMachineSwitch.scala 22:22]
    node _T = eq(UInt<3>("h0"), state) @[Conditional.scala 37:30]
    node _GEN_0 = mux(io_nickel, UInt<3>("h1"), state) @[VendingMachineSwitch.scala 26:24 VendingMachineSwitch.scala 26:32 VendingMachineSwitch.scala 22:22]
    node _GEN_1 = mux(io_dime, UInt<3>("h2"), _GEN_0) @[VendingMachineSwitch.scala 27:22 VendingMachineSwitch.scala 27:30]
    node _T_1 = eq(UInt<3>("h1"), state) @[Conditional.scala 37:30]
    node _GEN_2 = mux(io_nickel, UInt<3>("h2"), state) @[VendingMachineSwitch.scala 30:24 VendingMachineSwitch.scala 30:32 VendingMachineSwitch.scala 22:22]
    node _GEN_3 = mux(io_dime, UInt<3>("h3"), _GEN_2) @[VendingMachineSwitch.scala 31:22 VendingMachineSwitch.scala 31:30]
    node _T_2 = eq(UInt<3>("h2"), state) @[Conditional.scala 37:30]
    node _GEN_4 = mux(io_nickel, UInt<3>("h3"), state) @[VendingMachineSwitch.scala 34:24 VendingMachineSwitch.scala 34:32 VendingMachineSwitch.scala 22:22]
    node _GEN_5 = mux(io_dime, UInt<3>("h4"), _GEN_4) @[VendingMachineSwitch.scala 35:22 VendingMachineSwitch.scala 35:30]
    node _T_3 = eq(UInt<3>("h3"), state) @[Conditional.scala 37:30]
    node _GEN_6 = mux(io_nickel, UInt<3>("h4"), state) @[VendingMachineSwitch.scala 38:24 VendingMachineSwitch.scala 38:32 VendingMachineSwitch.scala 22:22]
    node _GEN_7 = mux(io_dime, UInt<3>("h4"), _GEN_6) @[VendingMachineSwitch.scala 39:22 VendingMachineSwitch.scala 39:30]
    node _T_4 = eq(UInt<3>("h4"), state) @[Conditional.scala 37:30]
    node _GEN_8 = mux(_T_4, UInt<3>("h0"), state) @[Conditional.scala 39:67 VendingMachineSwitch.scala 42:13 VendingMachineSwitch.scala 22:22]
    node _GEN_9 = mux(_T_3, _GEN_7, _GEN_8) @[Conditional.scala 39:67]
    node _GEN_10 = mux(_T_2, _GEN_5, _GEN_9) @[Conditional.scala 39:67]
    node _GEN_11 = mux(_T_1, _GEN_3, _GEN_10) @[Conditional.scala 39:67]
    node _GEN_12 = mux(_T, _GEN_1, _GEN_11) @[Conditional.scala 40:58]
    node _T_5 = eq(state, UInt<3>("h4")) @[VendingMachineSwitch.scala 45:22]
    io_valid <= _T_5 @[VendingMachineSwitch.scala 45:12]
    state <= mux(reset, UInt<3>("h0"), _GEN_12) @[VendingMachineSwitch.scala 22:22 VendingMachineSwitch.scala 22:22]