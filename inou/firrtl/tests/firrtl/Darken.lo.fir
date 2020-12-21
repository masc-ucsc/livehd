circuit Darken :
  module Darken :
    input clock : Clock
    input reset : UInt<1>
    input io_in : UInt<8>
    output io_out : UInt<8>

    node _T = dshl(io_in, UInt<1>("h1")) @[Darken.scala 12:19]
    io_out <= bits(_T, 7, 0) @[Darken.scala 12:10]