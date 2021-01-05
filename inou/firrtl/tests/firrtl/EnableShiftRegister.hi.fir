circuit EnableShiftRegister :
  module EnableShiftRegister :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip in : UInt<4>, flip shift : UInt<1>, out : UInt<4>}

    reg r0 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), r0) @[EnableShiftRegister.scala 12:15]
    reg r1 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), r1) @[EnableShiftRegister.scala 13:15]
    reg r2 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), r2) @[EnableShiftRegister.scala 14:15]
    reg r3 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), r3) @[EnableShiftRegister.scala 15:15]
    node _T = bits(reset, 0, 0) @[EnableShiftRegister.scala 16:14]
    when _T : @[EnableShiftRegister.scala 16:22]
      r0 <= UInt<4>("h0") @[EnableShiftRegister.scala 17:8]
      r1 <= UInt<4>("h0") @[EnableShiftRegister.scala 18:8]
      r2 <= UInt<4>("h0") @[EnableShiftRegister.scala 19:8]
      r3 <= UInt<4>("h0") @[EnableShiftRegister.scala 20:8]
    else :
      when io.shift : @[EnableShiftRegister.scala 21:25]
        r0 <= io.in @[EnableShiftRegister.scala 22:8]
        r1 <= r0 @[EnableShiftRegister.scala 23:8]
        r2 <= r1 @[EnableShiftRegister.scala 24:8]
        r3 <= r2 @[EnableShiftRegister.scala 25:8]
    io.out <= r3 @[EnableShiftRegister.scala 27:10]