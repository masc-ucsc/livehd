circuit ByteSelector :
  module ByteSelector :
    input clock : Clock
    input reset : UInt<1>
    input io_in : UInt<32>
    input io_offset : UInt<2>
    output io_out : UInt<8>

    node _T = eq(io_offset, UInt<2>("h0")) @[ByteSelector.scala 13:19]
    node _T_1 = bits(io_in, 7, 0) @[ByteSelector.scala 14:20]
    node _T_2 = eq(io_offset, UInt<1>("h1")) @[ByteSelector.scala 15:26]
    node _T_3 = bits(io_in, 15, 8) @[ByteSelector.scala 16:20]
    node _T_4 = eq(io_offset, UInt<2>("h2")) @[ByteSelector.scala 17:26]
    node _T_5 = bits(io_in, 23, 16) @[ByteSelector.scala 18:20]
    node _T_6 = bits(io_in, 31, 24) @[ByteSelector.scala 20:20]
    node _GEN_0 = mux(_T_4, _T_5, _T_6) @[ByteSelector.scala 17:35 ByteSelector.scala 18:12 ByteSelector.scala 20:12]
    node _GEN_1 = mux(_T_2, _T_3, _GEN_0) @[ByteSelector.scala 15:35 ByteSelector.scala 16:12]
    node _GEN_2 = mux(_T, _T_1, _GEN_1) @[ByteSelector.scala 13:33 ByteSelector.scala 14:12]
    io_out <= _GEN_2