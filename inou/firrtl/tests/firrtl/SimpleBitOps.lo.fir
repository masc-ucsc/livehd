circuit SimpleBitOps :
  module SimpleBitOps :
    input clock : Clock
    input reset : UInt<1>
    input io_inp1 : UInt<4>
    input io_inp2 : UInt<4>
    output io_out_and : UInt<4>
    output io_out_andr : UInt<1>
    output io_out_xor : UInt<4>
    output io_out_xorr : UInt<1>
    output io_out_or : UInt<4>
    output io_out_orr : UInt<1>
    output io_out_not : UInt<4>

    node _io_out_and_T = and(io_inp1, io_inp2) @[SimpleBitOps.scala 32:25]
    node _io_out_xor_T = xor(io_inp1, io_inp2) @[SimpleBitOps.scala 33:25]
    node _io_out_or_T = or(io_inp1, io_inp2) @[SimpleBitOps.scala 34:25]
    node _io_out_andr_T = andr(io_inp1) @[SimpleBitOps.scala 36:26]
    node _io_out_xorr_T = xorr(io_inp1) @[SimpleBitOps.scala 37:26]
    node _io_out_orr_T = orr(io_inp1) @[SimpleBitOps.scala 38:26]
    node _io_out_not_T = not(io_inp1) @[SimpleBitOps.scala 40:17]
    io_out_and <= _io_out_and_T @[SimpleBitOps.scala 32:14]
    io_out_andr <= _io_out_andr_T @[SimpleBitOps.scala 36:15]
    io_out_xor <= _io_out_xor_T @[SimpleBitOps.scala 33:14]
    io_out_xorr <= _io_out_xorr_T @[SimpleBitOps.scala 37:15]
    io_out_or <= _io_out_or_T @[SimpleBitOps.scala 34:14]
    io_out_orr <= _io_out_orr_T @[SimpleBitOps.scala 38:15]
    io_out_not <= _io_out_not_T @[SimpleBitOps.scala 40:14]