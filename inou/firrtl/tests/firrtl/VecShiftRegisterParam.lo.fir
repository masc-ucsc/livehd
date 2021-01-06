circuit VecShiftRegisterParam :
  module VecShiftRegisterParam :
    input clock : Clock
    input reset : UInt<1>
    input io_in : UInt<4>
    output io_out : UInt<4>

    reg delays_0 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), delays_0) @[VecShiftRegisterParam.scala 19:23]
    reg delays_1 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), delays_1) @[VecShiftRegisterParam.scala 19:23]
    reg delays_2 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), delays_2) @[VecShiftRegisterParam.scala 19:23]
    reg delays_3 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), delays_3) @[VecShiftRegisterParam.scala 19:23]
    reg delays_4 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), delays_4) @[VecShiftRegisterParam.scala 19:23]
    reg delays_5 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), delays_5) @[VecShiftRegisterParam.scala 19:23]
    reg delays_6 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), delays_6) @[VecShiftRegisterParam.scala 19:23]
    reg delays_7 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), delays_7) @[VecShiftRegisterParam.scala 19:23]
    node _T_0 = UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31 VecShiftRegisterParam.scala 19:31]
    node _T_1 = UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31 VecShiftRegisterParam.scala 19:31]
    node _T_2 = UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31 VecShiftRegisterParam.scala 19:31]
    node _T_3 = UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31 VecShiftRegisterParam.scala 19:31]
    node _T_4 = UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31 VecShiftRegisterParam.scala 19:31]
    node _T_5 = UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31 VecShiftRegisterParam.scala 19:31]
    node _T_6 = UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31 VecShiftRegisterParam.scala 19:31]
    node _T_7 = UInt<4>("h0") @[VecShiftRegisterParam.scala 19:31 VecShiftRegisterParam.scala 19:31]
    io_out <= delays_7 @[VecShiftRegisterParam.scala 26:10]
    delays_0 <= mux(reset, _T_0, io_in) @[VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 25:13]
    delays_1 <= mux(reset, _T_1, delays_0) @[VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 22:15]
    delays_2 <= mux(reset, _T_2, delays_1) @[VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 22:15]
    delays_3 <= mux(reset, _T_3, delays_2) @[VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 22:15]
    delays_4 <= mux(reset, _T_4, delays_3) @[VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 22:15]
    delays_5 <= mux(reset, _T_5, delays_4) @[VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 22:15]
    delays_6 <= mux(reset, _T_6, delays_5) @[VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 22:15]
    delays_7 <= mux(reset, _T_7, delays_6) @[VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 19:23 VecShiftRegisterParam.scala 22:15]