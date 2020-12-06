circuit Test6 :
  module Test6 :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip in : UInt<16>[20], flip addr : UInt<5>, out : UInt<16>}

    io.out <= io.in[io.addr] @[Test6.scala 17:10]