circuit Test3 :
  module Test3 :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip inp : UInt<12>, out_pad : UInt<16>}

    node _T = pad(io.inp, 16) @[Test3.scala 18:27]
    io.out_pad <= _T @[Test3.scala 18:14]