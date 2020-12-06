circuit SimpleBitOps :
  module SimpleBitOps :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip inp1 : UInt<4>, flip inp2 : UInt<4>, out_and : UInt<4>, out_andr : UInt<1>, out_xor : UInt<4>, out_xorr : UInt<1>, out_or : UInt<4>, out_orr : UInt<1>, out_not : UInt<4>}

    node _T = and(io.inp1, io.inp2) @[SimpleBitOps.scala 32:25]
    io.out_and <= _T @[SimpleBitOps.scala 32:14]
    node _T_1 = xor(io.inp1, io.inp2) @[SimpleBitOps.scala 33:25]
    io.out_xor <= _T_1 @[SimpleBitOps.scala 33:14]
    node _T_2 = or(io.inp1, io.inp2) @[SimpleBitOps.scala 34:25]
    io.out_or <= _T_2 @[SimpleBitOps.scala 34:14]
    node _T_3 = andr(io.inp1) @[SimpleBitOps.scala 36:26]
    io.out_andr <= _T_3 @[SimpleBitOps.scala 36:15]
    node _T_4 = xorr(io.inp1) @[SimpleBitOps.scala 37:26]
    io.out_xorr <= _T_4 @[SimpleBitOps.scala 37:15]
    node _T_5 = orr(io.inp1) @[SimpleBitOps.scala 38:26]
    io.out_orr <= _T_5 @[SimpleBitOps.scala 38:15]
    node _T_6 = not(io.inp1) @[SimpleBitOps.scala 40:17]
    io.out_not <= _T_6 @[SimpleBitOps.scala 40:14]