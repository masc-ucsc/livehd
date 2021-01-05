circuit VecSearch :
  module VecSearch :
    input clock : Clock
    input reset : UInt<1>
    output io : { out : UInt<4>}

    reg index : UInt<3>, clock with :
      reset => (reset, UInt<3>("h0")) @[VecSearch.scala 14:22]
    wire elts : UInt<4>[7] @[VecSearch.scala 15:22]
    elts[0] <= UInt<4>("h0") @[VecSearch.scala 15:22]
    elts[1] <= UInt<4>("h4") @[VecSearch.scala 15:22]
    elts[2] <= UInt<4>("hf") @[VecSearch.scala 15:22]
    elts[3] <= UInt<4>("he") @[VecSearch.scala 15:22]
    elts[4] <= UInt<4>("h2") @[VecSearch.scala 15:22]
    elts[5] <= UInt<4>("h5") @[VecSearch.scala 15:22]
    elts[6] <= UInt<4>("hd") @[VecSearch.scala 15:22]
    node _T = add(index, UInt<1>("h1")) @[VecSearch.scala 16:18]
    node _T_1 = tail(_T, 1) @[VecSearch.scala 16:18]
    index <= _T_1 @[VecSearch.scala 16:9]
    io.out <= elts[index] @[VecSearch.scala 17:10]