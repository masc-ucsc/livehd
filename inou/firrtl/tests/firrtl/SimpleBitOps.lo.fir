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

    node _T = and(io_inp1, io_inp2) @[SimpleBitOps.scala 32:25]
    node _T_1 = xor(io_inp1, io_inp2) @[SimpleBitOps.scala 33:25]
    node _T_2 = or(io_inp1, io_inp2) @[SimpleBitOps.scala 34:25]
    node _T_3 = andr(io_inp1) @[SimpleBitOps.scala 36:26]
    node _T_4 = xorr(io_inp1) @[SimpleBitOps.scala 37:26]
    node _T_5 = orr(io_inp1) @[SimpleBitOps.scala 38:26]
    node _T_6 = not(io_inp1) @[SimpleBitOps.scala 40:17]
    io_out_and <= _T @[SimpleBitOps.scala 32:14]
    io_out_andr <= _T_3 @[SimpleBitOps.scala 36:15]
    io_out_xor <= _T_1 @[SimpleBitOps.scala 33:14]
    io_out_xorr <= _T_4 @[SimpleBitOps.scala 37:15]
    io_out_or <= _T_2 @[SimpleBitOps.scala 34:14]
    io_out_orr <= _T_5 @[SimpleBitOps.scala 38:15]
    io_out_not <= _T_6 @[SimpleBitOps.scala 40:14]