circuit VecShiftRegisterParam :
  module VecShiftRegisterParam :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip in : UInt<4>, out : UInt<4>}

    wire _T : UInt<4>[8] @[VecShiftRegisterParam.scala 19:31]
    _T[0] <= UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31]
    _T[1] <= UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31]
    _T[2] <= UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31]
    _T[3] <= UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31]
    _T[4] <= UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31]
    _T[5] <= UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31]
    _T[6] <= UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31]
    _T[7] <= UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31]
    reg delays : UInt<4>[8], clock with :
      reset => (reset, _T) @[VecShiftRegisterParam.scala 19:23]
    delays[7] <= delays[6] @[VecShiftRegisterParam.scala 22:15]
    delays[6] <= delays[5] @[VecShiftRegisterParam.scala 22:15]
    delays[5] <= delays[4] @[VecShiftRegisterParam.scala 22:15]
    delays[4] <= delays[3] @[VecShiftRegisterParam.scala 22:15]
    delays[3] <= delays[2] @[VecShiftRegisterParam.scala 22:15]
    delays[2] <= delays[1] @[VecShiftRegisterParam.scala 22:15]
    delays[1] <= delays[0] @[VecShiftRegisterParam.scala 22:15]
    delays[0] <= io.in @[VecShiftRegisterParam.scala 25:13]
    io.out <= delays[7] @[VecShiftRegisterParam.scala 26:10]