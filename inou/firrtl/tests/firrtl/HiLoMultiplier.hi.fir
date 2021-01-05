circuit HiLoMultiplier :
  module HiLoMultiplier :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip A : UInt<4>, flip B : UInt<4>, Hi : UInt<4>, Lo : UInt<4>}

    node mult = mul(io.A, io.B) @[HiLoMultiplier.scala 14:19]
    node _T = bits(mult, 3, 0) @[HiLoMultiplier.scala 15:16]
    io.Lo <= _T @[HiLoMultiplier.scala 15:9]
    node _T_1 = bits(mult, 7, 4) @[HiLoMultiplier.scala 16:16]
    io.Hi <= _T_1 @[HiLoMultiplier.scala 16:9]