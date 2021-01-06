circuit Test6 :
  module Test6 :
    input clock : Clock
    input reset : UInt<1>
    input io_in_0 : UInt<16>
    input io_in_1 : UInt<16>
    input io_in_2 : UInt<16>
    input io_addr : UInt<5>
    output io_out : UInt<16>

    node _io_out_T = bits(io_addr, 1, 0)
    node _GEN_0 = validif(eq(UInt<1>("h0"), _io_out_T), io_in_0) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_1 = mux(eq(UInt<1>("h1"), _io_out_T), io_in_1, _GEN_0) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_2 = mux(eq(UInt<2>("h2"), _io_out_T), io_in_2, _GEN_1) @[Test6.scala 17:10 Test6.scala 17:10]
    node _io_in_io_out_T = _GEN_2 @[Test6.scala 17:10]
    io_out <= _io_in_io_out_T @[Test6.scala 17:10]