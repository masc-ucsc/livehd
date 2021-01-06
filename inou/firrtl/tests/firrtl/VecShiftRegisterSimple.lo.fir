circuit VecShiftRegisterSimple :
  module VecShiftRegisterSimple :
    input clock : Clock
    input reset : UInt<1>
    input io_in : UInt<8>
    output io_out : UInt<8>

    reg delays_0 : UInt<8>, clock with :
      reset => (UInt<1>("h0"), delays_0) @[VecShiftRegisterSimple.scala 18:23]
    reg delays_1 : UInt<8>, clock with :
      reset => (UInt<1>("h0"), delays_1) @[VecShiftRegisterSimple.scala 18:23]
    reg delays_2 : UInt<8>, clock with :
      reset => (UInt<1>("h0"), delays_2) @[VecShiftRegisterSimple.scala 18:23]
    reg delays_3 : UInt<8>, clock with :
      reset => (UInt<1>("h0"), delays_3) @[VecShiftRegisterSimple.scala 18:23]
    node _T_0 = UInt<8>("h0") @[VecShiftRegisterSimple.scala 18:31 VecShiftRegisterSimple.scala 18:31]
    node _T_1 = UInt<8>("h0") @[VecShiftRegisterSimple.scala 18:31 VecShiftRegisterSimple.scala 18:31]
    node _T_2 = UInt<8>("h0") @[VecShiftRegisterSimple.scala 18:31 VecShiftRegisterSimple.scala 18:31]
    node _T_3 = UInt<8>("h0") @[VecShiftRegisterSimple.scala 18:31 VecShiftRegisterSimple.scala 18:31]
    io_out <= delays_3 @[VecShiftRegisterSimple.scala 24:13]
    delays_0 <= mux(reset, _T_0, io_in) @[VecShiftRegisterSimple.scala 18:23 VecShiftRegisterSimple.scala 18:23 VecShiftRegisterSimple.scala 20:13]
    delays_1 <= mux(reset, _T_1, delays_0) @[VecShiftRegisterSimple.scala 18:23 VecShiftRegisterSimple.scala 18:23 VecShiftRegisterSimple.scala 21:13]
    delays_2 <= mux(reset, _T_2, delays_1) @[VecShiftRegisterSimple.scala 18:23 VecShiftRegisterSimple.scala 18:23 VecShiftRegisterSimple.scala 22:13]
    delays_3 <= mux(reset, _T_3, delays_2) @[VecShiftRegisterSimple.scala 18:23 VecShiftRegisterSimple.scala 18:23 VecShiftRegisterSimple.scala 23:13]