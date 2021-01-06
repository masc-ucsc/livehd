circuit Trivial :
  module Trivial :
    input clock : Clock
    input reset : UInt<1>
    output io_outp : UInt<4>

    io_outp <= UInt<3>("h5") @[Trivial.scala 22:11]