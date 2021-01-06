circuit Test1 :
  module Test1 :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip mux_value1 : UInt<16>, flip mux_value2 : UInt<16>, flip mux_value3 : UInt<16>, flip mux_sel1 : UInt<1>, flip mux_sel2 : UInt<1>, flip vec_set : UInt<1>, flip vec_subAcc : UInt<2>, flip vec : UInt<16>[4], mux_out : UInt<16>, vec_subInd_out : UInt<16>, vec_subAcc_out : UInt<16>}

    node _io_mux_out_T = mux(io.mux_sel2, io.mux_value2, io.mux_value3) @[Test1.scala 31:52]
    node _io_mux_out_T_1 = mux(io.mux_sel1, io.mux_value1, _io_mux_out_T) @[Test1.scala 31:20]
    io.mux_out <= _io_mux_out_T_1 @[Test1.scala 31:14]
    when io.vec_set : @[Test1.scala 33:21]
      io.vec_subInd_out <= io.vec[1] @[Test1.scala 34:23]
      io.vec_subAcc_out <= io.vec[io.vec_subAcc] @[Test1.scala 35:23]
    else :
      io.vec_subInd_out <= UInt<1>("h0") @[Test1.scala 37:23]
      io.vec_subAcc_out <= UInt<1>("h0") @[Test1.scala 38:23]