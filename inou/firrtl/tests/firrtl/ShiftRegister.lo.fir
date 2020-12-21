circuit ShiftRegister :
  module ShiftRegister :
    input clock : Clock
    input reset : UInt<1>
    input io_in : UInt<1>
    output io_out : UInt<1>

    reg r0 : UInt<1>, clock with :
      reset => (UInt<1>("h0"), r0) @[ShiftRegister.scala 11:19]
    reg r1 : UInt<1>, clock with :
      reset => (UInt<1>("h0"), r1) @[ShiftRegister.scala 12:19]
    reg r2 : UInt<1>, clock with :
      reset => (UInt<1>("h0"), r2) @[ShiftRegister.scala 13:19]
    reg r3 : UInt<1>, clock with :
      reset => (UInt<1>("h0"), r3) @[ShiftRegister.scala 14:19]
    io_out <= r3 @[ShiftRegister.scala 15:10]
    r0 <= io_in @[ShiftRegister.scala 11:19]
    r1 <= r0 @[ShiftRegister.scala 12:19]
    r2 <= r1 @[ShiftRegister.scala 13:19]
    r3 <= r2 @[ShiftRegister.scala 14:19]