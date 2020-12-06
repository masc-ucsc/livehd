circuit Test6 :
  module Test6 :
    input clock : Clock
    input reset : UInt<1>
    input io_in_0 : UInt<16>
    input io_in_1 : UInt<16>
    input io_in_2 : UInt<16>
    input io_in_3 : UInt<16>
    input io_in_4 : UInt<16>
    input io_in_5 : UInt<16>
    input io_in_6 : UInt<16>
    input io_in_7 : UInt<16>
    input io_in_8 : UInt<16>
    input io_in_9 : UInt<16>
    input io_in_10 : UInt<16>
    input io_in_11 : UInt<16>
    input io_in_12 : UInt<16>
    input io_in_13 : UInt<16>
    input io_in_14 : UInt<16>
    input io_in_15 : UInt<16>
    input io_in_16 : UInt<16>
    input io_in_17 : UInt<16>
    input io_in_18 : UInt<16>
    input io_in_19 : UInt<16>
    input io_addr : UInt<5>
    output io_out : UInt<16>

    node _GEN_0 = validif(eq(UInt<1>("h0"), io_addr), io_in_0) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_1 = mux(eq(UInt<1>("h1"), io_addr), io_in_1, _GEN_0) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_2 = mux(eq(UInt<2>("h2"), io_addr), io_in_2, _GEN_1) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_3 = mux(eq(UInt<2>("h3"), io_addr), io_in_3, _GEN_2) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_4 = mux(eq(UInt<3>("h4"), io_addr), io_in_4, _GEN_3) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_5 = mux(eq(UInt<3>("h5"), io_addr), io_in_5, _GEN_4) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_6 = mux(eq(UInt<3>("h6"), io_addr), io_in_6, _GEN_5) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_7 = mux(eq(UInt<3>("h7"), io_addr), io_in_7, _GEN_6) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_8 = mux(eq(UInt<4>("h8"), io_addr), io_in_8, _GEN_7) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_9 = mux(eq(UInt<4>("h9"), io_addr), io_in_9, _GEN_8) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_10 = mux(eq(UInt<4>("ha"), io_addr), io_in_10, _GEN_9) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_11 = mux(eq(UInt<4>("hb"), io_addr), io_in_11, _GEN_10) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_12 = mux(eq(UInt<4>("hc"), io_addr), io_in_12, _GEN_11) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_13 = mux(eq(UInt<4>("hd"), io_addr), io_in_13, _GEN_12) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_14 = mux(eq(UInt<4>("he"), io_addr), io_in_14, _GEN_13) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_15 = mux(eq(UInt<4>("hf"), io_addr), io_in_15, _GEN_14) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_16 = mux(eq(UInt<5>("h10"), io_addr), io_in_16, _GEN_15) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_17 = mux(eq(UInt<5>("h11"), io_addr), io_in_17, _GEN_16) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_18 = mux(eq(UInt<5>("h12"), io_addr), io_in_18, _GEN_17) @[Test6.scala 17:10 Test6.scala 17:10]
    node _GEN_19 = mux(eq(UInt<5>("h13"), io_addr), io_in_19, _GEN_18) @[Test6.scala 17:10 Test6.scala 17:10]
    node _io_in_io_addr = _GEN_19 @[Test6.scala 17:10]
    io_out <= _io_in_io_addr @[Test6.scala 17:10]