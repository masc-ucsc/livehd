circuit Mul :
  module Mul :
    input clock : Clock
    input reset : UInt<1>
    input io_x : UInt<2>
    input io_y : UInt<2>
    output io_z : UInt<4>

    node _io_z_T = dshl(io_x, UInt<2>("h2")) @[Mul.scala 25:21]
    node _io_z_T_1 = or(_io_z_T, io_y) @[Mul.scala 25:29]
    node _io_z_T_2 = bits(_io_z_T_1, 3, 0)
    node tbl_0 = UInt<4>("h0") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_0 = validif(eq(UInt<1>("h0"), _io_z_T_2), tbl_0) @[Mul.scala 25:8 Mul.scala 25:8]
    node tbl_1 = UInt<4>("h0") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_1 = mux(eq(UInt<1>("h1"), _io_z_T_2), tbl_1, _GEN_0) @[Mul.scala 25:8 Mul.scala 25:8]
    node tbl_2 = UInt<4>("h0") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_2 = mux(eq(UInt<2>("h2"), _io_z_T_2), tbl_2, _GEN_1) @[Mul.scala 25:8 Mul.scala 25:8]
    node tbl_3 = UInt<4>("h0") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_3 = mux(eq(UInt<2>("h3"), _io_z_T_2), tbl_3, _GEN_2) @[Mul.scala 25:8 Mul.scala 25:8]
    node tbl_4 = UInt<4>("h0") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_4 = mux(eq(UInt<3>("h4"), _io_z_T_2), tbl_4, _GEN_3) @[Mul.scala 25:8 Mul.scala 25:8]
    node tbl_5 = UInt<4>("h1") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_5 = mux(eq(UInt<3>("h5"), _io_z_T_2), tbl_5, _GEN_4) @[Mul.scala 25:8 Mul.scala 25:8]
    node tbl_6 = UInt<4>("h2") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_6 = mux(eq(UInt<3>("h6"), _io_z_T_2), tbl_6, _GEN_5) @[Mul.scala 25:8 Mul.scala 25:8]
    node tbl_7 = UInt<4>("h3") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_7 = mux(eq(UInt<3>("h7"), _io_z_T_2), tbl_7, _GEN_6) @[Mul.scala 25:8 Mul.scala 25:8]
    node tbl_8 = UInt<4>("h0") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_8 = mux(eq(UInt<4>("h8"), _io_z_T_2), tbl_8, _GEN_7) @[Mul.scala 25:8 Mul.scala 25:8]
    node tbl_9 = UInt<4>("h2") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_9 = mux(eq(UInt<4>("h9"), _io_z_T_2), tbl_9, _GEN_8) @[Mul.scala 25:8 Mul.scala 25:8]
    node tbl_10 = UInt<4>("h4") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_10 = mux(eq(UInt<4>("ha"), _io_z_T_2), tbl_10, _GEN_9) @[Mul.scala 25:8 Mul.scala 25:8]
    node tbl_11 = UInt<4>("h6") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_11 = mux(eq(UInt<4>("hb"), _io_z_T_2), tbl_11, _GEN_10) @[Mul.scala 25:8 Mul.scala 25:8]
    node tbl_12 = UInt<4>("h0") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_12 = mux(eq(UInt<4>("hc"), _io_z_T_2), tbl_12, _GEN_11) @[Mul.scala 25:8 Mul.scala 25:8]
    node tbl_13 = UInt<4>("h3") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_13 = mux(eq(UInt<4>("hd"), _io_z_T_2), tbl_13, _GEN_12) @[Mul.scala 25:8 Mul.scala 25:8]
    node tbl_14 = UInt<4>("h6") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_14 = mux(eq(UInt<4>("he"), _io_z_T_2), tbl_14, _GEN_13) @[Mul.scala 25:8 Mul.scala 25:8]
    node tbl_15 = UInt<4>("h9") @[Mul.scala 24:20 Mul.scala 24:20]
    node _GEN_15 = mux(eq(UInt<4>("hf"), _io_z_T_2), tbl_15, _GEN_14) @[Mul.scala 25:8 Mul.scala 25:8]
    node _tbl_io_z_T_2 = _GEN_15 @[Mul.scala 25:8]
    io_z <= _tbl_io_z_T_2 @[Mul.scala 25:8]