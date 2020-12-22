circuit VecSearch :
  module VecSearch :
    input clock : Clock
    input reset : UInt<1>
    output io_out : UInt<4>

    reg index : UInt<3>, clock with :
      reset => (UInt<1>("h0"), index) @[VecSearch.scala 14:22]
    node _T = add(index, UInt<1>("h1")) @[VecSearch.scala 16:18]
    node _T_1 = tail(_T, 1) @[VecSearch.scala 16:18]
    node elts_0 = UInt<4>("h0") @[VecSearch.scala 15:22 VecSearch.scala 15:22]
    node _GEN_0 = validif(eq(UInt<1>("h0"), index), elts_0) @[VecSearch.scala 17:10 VecSearch.scala 17:10]
    node elts_1 = UInt<4>("h4") @[VecSearch.scala 15:22 VecSearch.scala 15:22]
    node _GEN_1 = mux(eq(UInt<1>("h1"), index), elts_1, _GEN_0) @[VecSearch.scala 17:10 VecSearch.scala 17:10]
    node elts_2 = UInt<4>("hf") @[VecSearch.scala 15:22 VecSearch.scala 15:22]
    node _GEN_2 = mux(eq(UInt<2>("h2"), index), elts_2, _GEN_1) @[VecSearch.scala 17:10 VecSearch.scala 17:10]
    node elts_3 = UInt<4>("he") @[VecSearch.scala 15:22 VecSearch.scala 15:22]
    node _GEN_3 = mux(eq(UInt<2>("h3"), index), elts_3, _GEN_2) @[VecSearch.scala 17:10 VecSearch.scala 17:10]
    node elts_4 = UInt<4>("h2") @[VecSearch.scala 15:22 VecSearch.scala 15:22]
    node _GEN_4 = mux(eq(UInt<3>("h4"), index), elts_4, _GEN_3) @[VecSearch.scala 17:10 VecSearch.scala 17:10]
    node elts_5 = UInt<4>("h5") @[VecSearch.scala 15:22 VecSearch.scala 15:22]
    node _GEN_5 = mux(eq(UInt<3>("h5"), index), elts_5, _GEN_4) @[VecSearch.scala 17:10 VecSearch.scala 17:10]
    node elts_6 = UInt<4>("hd") @[VecSearch.scala 15:22 VecSearch.scala 15:22]
    node _GEN_6 = mux(eq(UInt<3>("h6"), index), elts_6, _GEN_5) @[VecSearch.scala 17:10 VecSearch.scala 17:10]
    node _elts_index = _GEN_6 @[VecSearch.scala 17:10]
    io_out <= _elts_index @[VecSearch.scala 17:10]
    index <= mux(reset, UInt<3>("h0"), _T_1) @[VecSearch.scala 14:22 VecSearch.scala 14:22 VecSearch.scala 16:9]