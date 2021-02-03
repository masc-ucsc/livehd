circuit VecShiftRegister :
  module VecShiftRegister :
    input clock : Clock
    input reset : UInt<1>
    input io_ins_0 : UInt<4>
    input io_ins_1 : UInt<4>
    input io_load : UInt<1>
    input io_shift : UInt<1>
    output io_out : UInt<4>

    reg delays_0 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), delays_0) @[VecShiftRegister.scala 21:19]
    reg delays_1 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), delays_1) @[VecShiftRegister.scala 21:19]
    node _GEN_0 = mux(io_shift, io_ins_0, delays_0) @[VecShiftRegister.scala 27:25 VecShiftRegister.scala 28:15 VecShiftRegister.scala 21:19]
    node _GEN_1 = mux(io_shift, delays_0, delays_1) @[VecShiftRegister.scala 27:25 VecShiftRegister.scala 29:15 VecShiftRegister.scala 21:19]
    node _GEN_2 = mux(io_load, io_ins_0, _GEN_0) @[VecShiftRegister.scala 22:18 VecShiftRegister.scala 23:15]
    node _GEN_3 = mux(io_load, io_ins_1, _GEN_1) @[VecShiftRegister.scala 22:18 VecShiftRegister.scala 24:15]
    io_out <= delays_1 @[VecShiftRegister.scala 34:10]
    delays_0 <= _GEN_2
    delays_1 <= _GEN_3