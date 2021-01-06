circuit LFSR16 :
  module LFSR16 :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip inc : UInt<1>, out : UInt<16>}

    reg res : UInt<16>, clock with :
      reset => (reset, UInt<16>("h1")) @[LFSR16.scala 19:20]
    when io.inc : @[LFSR16.scala 20:17]
      node _T = bits(res, 0, 0) @[LFSR16.scala 21:26]
      node _T_1 = bits(res, 2, 2) @[LFSR16.scala 21:33]
      node _T_2 = xor(_T, _T_1) @[LFSR16.scala 21:29]
      node _T_3 = bits(res, 3, 3) @[LFSR16.scala 21:40]
      node _T_4 = xor(_T_2, _T_3) @[LFSR16.scala 21:36]
      node _T_5 = bits(res, 5, 5) @[LFSR16.scala 21:47]
      node _T_6 = xor(_T_4, _T_5) @[LFSR16.scala 21:43]
      node _T_7 = bits(res, 15, 1) @[LFSR16.scala 21:55]
      node _T_8 = cat(_T_6, _T_7) @[Cat.scala 29:58]
      res <= _T_8 @[LFSR16.scala 22:9]
    io.out <= res @[LFSR16.scala 24:10]