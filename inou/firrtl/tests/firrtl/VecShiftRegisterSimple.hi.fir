circuit VecShiftRegisterSimple :
  module VecShiftRegisterSimple :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip in : UInt<8>, out : UInt<8>}

    wire _T : UInt<8>[4] @[VecShiftRegisterSimple.scala 18:31]
    _T[0] <= UInt<8>("h0") @[VecShiftRegisterSimple.scala 18:31]
    _T[1] <= UInt<8>("h0") @[VecShiftRegisterSimple.scala 18:31]
    _T[2] <= UInt<8>("h0") @[VecShiftRegisterSimple.scala 18:31]
    _T[3] <= UInt<8>("h0") @[VecShiftRegisterSimple.scala 18:31]
    reg delays : UInt<8>[4], clock with :
      reset => (reset, _T) @[VecShiftRegisterSimple.scala 18:23]
    delays[0] <= io.in @[VecShiftRegisterSimple.scala 20:13]
    delays[1] <= delays[0] @[VecShiftRegisterSimple.scala 21:13]
    delays[2] <= delays[1] @[VecShiftRegisterSimple.scala 22:13]
    delays[3] <= delays[2] @[VecShiftRegisterSimple.scala 23:13]
    io.out <= delays[3] @[VecShiftRegisterSimple.scala 24:13]