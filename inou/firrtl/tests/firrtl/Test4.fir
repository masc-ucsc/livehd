circuit Test4 :
  module Test4 :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip inp : UInt<16>, out : SInt<16>}

    node _T = asSInt(io.inp) @[Test4.scala 18:20]
    io.out <= _T @[Test4.scala 18:10]