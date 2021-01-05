circuit VecShiftRegister :
  module VecShiftRegister :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip ins : UInt<4>[4], flip load : UInt<1>, flip shift : UInt<1>, out : UInt<4>}

    reg delays : UInt<4>[4], clock with :
      reset => (UInt<1>("h0"), delays) @[VecShiftRegister.scala 20:19]
    when io.load : @[VecShiftRegister.scala 21:18]
      delays[0] <= io.ins[0] @[VecShiftRegister.scala 22:15]
      delays[1] <= io.ins[1] @[VecShiftRegister.scala 23:15]
      delays[2] <= io.ins[2] @[VecShiftRegister.scala 24:15]
      delays[3] <= io.ins[3] @[VecShiftRegister.scala 25:15]
    else :
      when io.shift : @[VecShiftRegister.scala 26:25]
        delays[0] <= io.ins[0] @[VecShiftRegister.scala 27:15]
        delays[1] <= delays[0] @[VecShiftRegister.scala 28:15]
        delays[2] <= delays[1] @[VecShiftRegister.scala 29:15]
        delays[3] <= delays[2] @[VecShiftRegister.scala 30:15]
    io.out <= delays[3] @[VecShiftRegister.scala 32:10]