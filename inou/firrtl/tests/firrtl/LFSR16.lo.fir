circuit LFSR16 :
  module LFSR16 :
    input clock : Clock
    input reset : UInt<1>
    input io_inc : UInt<1>
    output io_out : UInt<16>

    reg res : UInt<16>, clock with :
      reset => (UInt<1>("h0"), res) @[LFSR16.scala 19:20]
    node _T = bits(res, 0, 0) @[LFSR16.scala 21:26]
    node _T_1 = bits(res, 2, 2) @[LFSR16.scala 21:33]
    node _T_2 = xor(_T, _T_1) @[LFSR16.scala 21:29]
    node _T_3 = bits(res, 3, 3) @[LFSR16.scala 21:40]
    node _T_4 = xor(_T_2, _T_3) @[LFSR16.scala 21:36]
    node _T_5 = bits(res, 5, 5) @[LFSR16.scala 21:47]
    node _T_6 = xor(_T_4, _T_5) @[LFSR16.scala 21:43]
    node _T_7 = bits(res, 15, 1) @[LFSR16.scala 21:55]
    node _T_8 = cat(_T_6, _T_7) @[Cat.scala 29:58]
    node _GEN_0 = mux(io_inc, _T_8, res) @[LFSR16.scala 20:17 LFSR16.scala 22:9 LFSR16.scala 19:20]
    io_out <= res @[LFSR16.scala 24:10]
    res <= mux(reset, UInt<16>("h1"), _GEN_0) @[LFSR16.scala 19:20 LFSR16.scala 19:20]