circuit HiLoMultiplier :
  module HiLoMultiplier :
    input clock : Clock
    input reset : UInt<1>
    input io_A : UInt<4>
    input io_B : UInt<4>
    output io_Hi : UInt<4>
    output io_Lo : UInt<4>

    node mult = mul(io_A, io_B) @[HiLoMultiplier.scala 14:19]
    node _T = bits(mult, 3, 0) @[HiLoMultiplier.scala 15:16]
    node _T_1 = bits(mult, 7, 4) @[HiLoMultiplier.scala 16:16]
    io_Hi <= _T_1 @[HiLoMultiplier.scala 16:9]
    io_Lo <= _T @[HiLoMultiplier.scala 15:9]