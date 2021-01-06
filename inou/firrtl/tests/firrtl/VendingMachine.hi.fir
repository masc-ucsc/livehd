circuit VendingMachine :
  module VendingMachine :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip nickel : UInt<1>, flip dime : UInt<1>, valid : UInt<1>}

    reg state : UInt<3>, clock with :
      reset => (reset, UInt<3>("h0")) @[VendingMachine.scala 22:22]
    node _T = eq(state, UInt<3>("h0")) @[VendingMachine.scala 23:15]
    when _T : @[VendingMachine.scala 23:26]
      when io.nickel : @[VendingMachine.scala 24:22]
        state <= UInt<3>("h1") @[VendingMachine.scala 24:30]
      when io.dime : @[VendingMachine.scala 25:22]
        state <= UInt<3>("h2") @[VendingMachine.scala 25:30]
    node _T_1 = eq(state, UInt<3>("h1")) @[VendingMachine.scala 27:15]
    when _T_1 : @[VendingMachine.scala 27:23]
      when io.nickel : @[VendingMachine.scala 28:22]
        state <= UInt<3>("h2") @[VendingMachine.scala 28:30]
      when io.dime : @[VendingMachine.scala 29:22]
        state <= UInt<3>("h3") @[VendingMachine.scala 29:30]
    node _T_2 = eq(state, UInt<3>("h2")) @[VendingMachine.scala 31:15]
    when _T_2 : @[VendingMachine.scala 31:24]
      when io.nickel : @[VendingMachine.scala 32:22]
        state <= UInt<3>("h3") @[VendingMachine.scala 32:30]
      when io.dime : @[VendingMachine.scala 33:22]
        state <= UInt<3>("h4") @[VendingMachine.scala 33:30]
    node _T_3 = eq(state, UInt<3>("h3")) @[VendingMachine.scala 35:15]
    when _T_3 : @[VendingMachine.scala 35:24]
      when io.nickel : @[VendingMachine.scala 36:22]
        state <= UInt<3>("h4") @[VendingMachine.scala 36:30]
      when io.dime : @[VendingMachine.scala 37:22]
        state <= UInt<3>("h4") @[VendingMachine.scala 37:30]
    node _T_4 = eq(state, UInt<3>("h4")) @[VendingMachine.scala 39:15]
    when _T_4 : @[VendingMachine.scala 39:24]
      state <= UInt<3>("h0") @[VendingMachine.scala 40:11]
    node _T_5 = eq(state, UInt<3>("h4")) @[VendingMachine.scala 42:22]
    io.valid <= _T_5 @[VendingMachine.scala 42:12]