circuit Test1 :
  module Test1 :
    input clock : Clock
    input reset : UInt<1>
    input io_vec_set : UInt<1>
    input io_vec_idx : UInt<2>
    input io_vec_ary_0 : UInt<16>
    input io_vec_ary_1 : UInt<16>
    input io_vec_ary_2 : UInt<16>
    input io_vec_ary_3 : UInt<16>
    output io_vec_ary_out : UInt<16>

    node _GEN_0 = validif(eq(UInt<1>("h0"), io_vec_idx), io_vec_ary_0) @[Test1.scala 35:20 Test1.scala 35:20]
    node _GEN_1 = mux(eq(UInt<1>("h1"), io_vec_idx), io_vec_ary_1, _GEN_0) @[Test1.scala 35:20 Test1.scala 35:20]
    node _GEN_2 = mux(eq(UInt<2>("h2"), io_vec_idx), io_vec_ary_2, _GEN_1) @[Test1.scala 35:20 Test1.scala 35:20]
    node _GEN_3 = mux(eq(UInt<2>("h3"), io_vec_idx), io_vec_ary_3, _GEN_2) @[Test1.scala 35:20 Test1.scala 35:20]
    node _io_vec_ary_io_vec_idx = _GEN_3 @[Test1.scala 35:20]
    node _GEN_4 = mux(io_vec_set, _io_vec_ary_io_vec_idx, UInt<1>("h0")) @[Test1.scala 33:21 Test1.scala 35:20 Test1.scala 38:20]
    io_vec_ary_out <= _GEN_4