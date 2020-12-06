circuit Test4 :
  module Test4 :
    input clock : Clock
    input reset : UInt<1>
    input io_inp : UInt<16>
    output io_out : SInt<16>

    node _io_out_T = asSInt(io_inp) @[Test4.scala 18:20]
    io_out <= _io_out_T @[Test4.scala 18:10]