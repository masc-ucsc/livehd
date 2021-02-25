circuit VecShiftRegister :
  module VecShiftRegister :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip ins : UInt<4>[2], flip load : UInt<1>, flip shift : UInt<1>, out : UInt<4>}

    reg delays : UInt<4>[2], clock with :
      reset => (UInt<1>("h0"), delays) @[VecShiftRegister.scala 21:19]

    when io.load : @[VecShiftRegister.scala 22:18]
      delays[0] <= io.ins[0] @[VecShiftRegister.scala 23:15]
      delays[1] <= io.ins[1] @[VecShiftRegister.scala 24:15]
    else :
      when io.shift : @[VecShiftRegister.scala 27:25]
        delays[0] <= io.ins[0] @[VecShiftRegister.scala 28:15]
        delays[1] <= delays[0] @[VecShiftRegister.scala 29:15]
    io.out <= delays[1] @[VecShiftRegister.scala 34:10]