circuit Test1 :
  module Test1 :
    input clock : Clock
    input reset : UInt<1>
    input io_mux_value1 : UInt<16>
    input io_mux_value2 : UInt<16>
    input io_mux_value3 : UInt<16>
    input io_mux_sel1 : UInt<1>
    input io_mux_sel2 : UInt<1>
    input io_vec_set : UInt<1>
    input io_vec_subAcc : UInt<2>
    input io_vec_0 : UInt<16>
    input io_vec_1 : UInt<16>
    input io_vec_2 : UInt<16>
    input io_vec_3 : UInt<16>
    output io_mux_out : UInt<16>
    output io_vec_subInd_out : UInt<16>
    output io_vec_subAcc_out : UInt<16>

    node _io_mux_out_T = mux(io_mux_sel2, io_mux_value2, io_mux_value3) @[Test1.scala 31:52]
    node _io_mux_out_T_1 = mux(io_mux_sel1, io_mux_value1, _io_mux_out_T) @[Test1.scala 31:20]
    node _GEN_0 = validif(eq(UInt<1>("h0"), io_vec_subAcc), io_vec_0) @[Test1.scala 35:23 Test1.scala 35:23]
    node _GEN_1 = mux(eq(UInt<1>("h1"), io_vec_subAcc), io_vec_1, _GEN_0) @[Test1.scala 35:23 Test1.scala 35:23]
    node _GEN_2 = mux(eq(UInt<2>("h2"), io_vec_subAcc), io_vec_2, _GEN_1) @[Test1.scala 35:23 Test1.scala 35:23]
    node _GEN_3 = mux(eq(UInt<2>("h3"), io_vec_subAcc), io_vec_3, _GEN_2) @[Test1.scala 35:23 Test1.scala 35:23]
    node _GEN_4 = mux(io_vec_set, io_vec_1, UInt<1>("h0")) @[Test1.scala 33:21 Test1.scala 34:23 Test1.scala 37:23]
    node _io_vec_io_vec_subAcc = _GEN_3 @[Test1.scala 35:23]
    node _GEN_5 = mux(io_vec_set, _io_vec_io_vec_subAcc, UInt<1>("h0")) @[Test1.scala 33:21 Test1.scala 35:23 Test1.scala 38:23]
    io_mux_out <= _io_mux_out_T_1 @[Test1.scala 31:14]
    io_vec_subInd_out <= _GEN_4
    io_vec_subAcc_out <= _GEN_5