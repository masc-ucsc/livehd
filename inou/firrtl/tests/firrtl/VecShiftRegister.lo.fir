circuit VecShiftRegister :
  module VecShiftRegister :
    input clock : Clock
    input reset : UInt<1>
    input io_ins_0 : UInt<4>
    input io_ins_1 : UInt<4>
    input io_ins_2 : UInt<4>
    input io_ins_3 : UInt<4>
    input io_load : UInt<1>
    input io_shift : UInt<1>
    output io_out : UInt<4>

    reg delays_0 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), delays_0) @[VecShiftRegister.scala 20:19]
    reg delays_1 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), delays_1) @[VecShiftRegister.scala 20:19]
    reg delays_2 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), delays_2) @[VecShiftRegister.scala 20:19]
    reg delays_3 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), delays_3) @[VecShiftRegister.scala 20:19]
    node _GEN_0 = mux(io_shift, io_ins_0, delays_0) @[VecShiftRegister.scala 26:25 VecShiftRegister.scala 27:15 VecShiftRegister.scala 20:19]
    node _GEN_1 = mux(io_shift, delays_0, delays_1) @[VecShiftRegister.scala 26:25 VecShiftRegister.scala 28:15 VecShiftRegister.scala 20:19]
    node _GEN_2 = mux(io_shift, delays_1, delays_2) @[VecShiftRegister.scala 26:25 VecShiftRegister.scala 29:15 VecShiftRegister.scala 20:19]
    node _GEN_3 = mux(io_shift, delays_2, delays_3) @[VecShiftRegister.scala 26:25 VecShiftRegister.scala 30:15 VecShiftRegister.scala 20:19]
    node _GEN_4 = mux(io_load, io_ins_0, _GEN_0) @[VecShiftRegister.scala 21:18 VecShiftRegister.scala 22:15]
    node _GEN_5 = mux(io_load, io_ins_1, _GEN_1) @[VecShiftRegister.scala 21:18 VecShiftRegister.scala 23:15]
    node _GEN_6 = mux(io_load, io_ins_2, _GEN_2) @[VecShiftRegister.scala 21:18 VecShiftRegister.scala 24:15]
    node _GEN_7 = mux(io_load, io_ins_3, _GEN_3) @[VecShiftRegister.scala 21:18 VecShiftRegister.scala 25:15]
    io_out <= delays_3 @[VecShiftRegister.scala 32:10]
    delays_0 <= _GEN_4
    delays_1 <= _GEN_5
    delays_2 <= _GEN_6
    delays_3 <= _GEN_7