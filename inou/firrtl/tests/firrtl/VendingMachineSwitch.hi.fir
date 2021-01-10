circuit VendingMachineSwitch :
  module VendingMachineSwitch :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip nickel : UInt<1>, flip dime : UInt<1>, valid : UInt<1>}

    reg state : UInt<3>, clock with :
      reset => (reset, UInt<3>("h0")) @[VendingMachineSwitch.scala 22:22]
    node _T = eq(UInt<3>("h0"), state) @[Conditional.scala 37:30]
    when _T : @[Conditional.scala 40:58]
      when io.nickel : @[VendingMachineSwitch.scala 26:24]
        state <= UInt<3>("h1") @[VendingMachineSwitch.scala 26:32]
      when io.dime : @[VendingMachineSwitch.scala 27:22]
        state <= UInt<3>("h2") @[VendingMachineSwitch.scala 27:30]
    else :
      node _T_1 = eq(UInt<3>("h1"), state) @[Conditional.scala 37:30]
      when _T_1 : @[Conditional.scala 39:67]
        when io.nickel : @[VendingMachineSwitch.scala 30:24]
          state <= UInt<3>("h2") @[VendingMachineSwitch.scala 30:32]
        when io.dime : @[VendingMachineSwitch.scala 31:22]
          state <= UInt<3>("h3") @[VendingMachineSwitch.scala 31:30]
      else :
        node _T_2 = eq(UInt<3>("h2"), state) @[Conditional.scala 37:30]
        when _T_2 : @[Conditional.scala 39:67]
          when io.nickel : @[VendingMachineSwitch.scala 34:24]
            state <= UInt<3>("h3") @[VendingMachineSwitch.scala 34:32]
          when io.dime : @[VendingMachineSwitch.scala 35:22]
            state <= UInt<3>("h4") @[VendingMachineSwitch.scala 35:30]
        else :
          node _T_3 = eq(UInt<3>("h3"), state) @[Conditional.scala 37:30]
          when _T_3 : @[Conditional.scala 39:67]
            when io.nickel : @[VendingMachineSwitch.scala 38:24]
              state <= UInt<3>("h4") @[VendingMachineSwitch.scala 38:32]
            when io.dime : @[VendingMachineSwitch.scala 39:22]
              state <= UInt<3>("h4") @[VendingMachineSwitch.scala 39:30]
          else :
            node _T_4 = eq(UInt<3>("h4"), state) @[Conditional.scala 37:30]
            when _T_4 : @[Conditional.scala 39:67]
              state <= UInt<3>("h0") @[VendingMachineSwitch.scala 42:13]
    node _T_5 = eq(state, UInt<3>("h4")) @[VendingMachineSwitch.scala 45:22]
    io.valid <= _T_5 @[VendingMachineSwitch.scala 45:12]