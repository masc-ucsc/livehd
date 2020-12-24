circuit EnableShiftRegister :
  module EnableShiftRegister :
    input clock : Clock
    input reset : UInt<1>
    input io_in : UInt<4>
    input io_shift : UInt<1>
    output io_out : UInt<4>

    reg r0 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), r0) @[EnableShiftRegister.scala 12:15]
    reg r1 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), r1) @[EnableShiftRegister.scala 13:15]
    reg r2 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), r2) @[EnableShiftRegister.scala 14:15]
    reg r3 : UInt<4>, clock with :
      reset => (UInt<1>("h0"), r3) @[EnableShiftRegister.scala 15:15]
    node _T = bits(reset, 0, 0) @[EnableShiftRegister.scala 16:14]
    node _GEN_0 = mux(io_shift, io_in, r0) @[EnableShiftRegister.scala 21:25 EnableShiftRegister.scala 22:8 EnableShiftRegister.scala 12:15]
    node _GEN_1 = mux(io_shift, r0, r1) @[EnableShiftRegister.scala 21:25 EnableShiftRegister.scala 23:8 EnableShiftRegister.scala 13:15]
    node _GEN_2 = mux(io_shift, r1, r2) @[EnableShiftRegister.scala 21:25 EnableShiftRegister.scala 24:8 EnableShiftRegister.scala 14:15]
    node _GEN_3 = mux(io_shift, r2, r3) @[EnableShiftRegister.scala 21:25 EnableShiftRegister.scala 25:8 EnableShiftRegister.scala 15:15]
    node _GEN_4 = mux(_T, UInt<4>("h0"), _GEN_0) @[EnableShiftRegister.scala 16:22 EnableShiftRegister.scala 17:8]
    node _GEN_5 = mux(_T, UInt<4>("h0"), _GEN_1) @[EnableShiftRegister.scala 16:22 EnableShiftRegister.scala 18:8]
    node _GEN_6 = mux(_T, UInt<4>("h0"), _GEN_2) @[EnableShiftRegister.scala 16:22 EnableShiftRegister.scala 19:8]
    node _GEN_7 = mux(_T, UInt<4>("h0"), _GEN_3) @[EnableShiftRegister.scala 16:22 EnableShiftRegister.scala 20:8]
    io_out <= r3 @[EnableShiftRegister.scala 27:10]
    r0 <= _GEN_4
    r1 <= _GEN_5
    r2 <= _GEN_6
    r3 <= _GEN_7