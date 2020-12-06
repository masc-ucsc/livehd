circuit Test4 :
  module Test4 :
    input clock : Clock
    input reset : UInt<1>
    input io_inp : UInt<16>
    output io_out : SInt<16>

    node _T = asSInt(io_inp) @[Test4.scala 18:20]
    io_out <= _T @[Test4.scala 18:10]