circuit ResetShiftRegister :
  module ResetShiftRegister :
    input clock : Clock
    input reset : UInt<1>
    input io_in : UInt<4>
    input io_shift : UInt<1>
    output io_out : UInt<4>

    reg r0 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), r0) @[ResetShiftRegister.scala 13:19]
    reg r1 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), r1) @[ResetShiftRegister.scala 14:19]
    reg r2 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), r2) @[ResetShiftRegister.scala 15:19]
    reg r3 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), r3) @[ResetShiftRegister.scala 16:19]
    node _GEN_0 = mux(io_shift, io_in, r0) @[ResetShiftRegister.scala 17:19 ResetShiftRegister.scala 18:8 ResetShiftRegister.scala 13:19]
    node _GEN_1 = mux(io_shift, r0, r1) @[ResetShiftRegister.scala 17:19 ResetShiftRegister.scala 19:8 ResetShiftRegister.scala 14:19]
    node _GEN_2 = mux(io_shift, r1, r2) @[ResetShiftRegister.scala 17:19 ResetShiftRegister.scala 20:8 ResetShiftRegister.scala 15:19]
    node _GEN_3 = mux(io_shift, r2, r3) @[ResetShiftRegister.scala 17:19 ResetShiftRegister.scala 21:8 ResetShiftRegister.scala 16:19]
    io_out <= r3 @[ResetShiftRegister.scala 23:10]
    r0 <= mux(reset, UInt<4>("h0"), _GEN_0) @[ResetShiftRegister.scala 13:19 ResetShiftRegister.scala 13:19]
    r1 <= mux(reset, UInt<4>("h0"), _GEN_1) @[ResetShiftRegister.scala 14:19 ResetShiftRegister.scala 14:19]
    r2 <= mux(reset, UInt<4>("h0"), _GEN_2) @[ResetShiftRegister.scala 15:19 ResetShiftRegister.scala 15:19]
    r3 <= mux(reset, UInt<4>("h0"), _GEN_3) @[ResetShiftRegister.scala 16:19 ResetShiftRegister.scala 16:19]