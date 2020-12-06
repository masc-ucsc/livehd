circuit Tail :
  module Tail :
    input io_a : UInt<3>
    output io_b : UInt<1>

    node _T = tail(io_a, 2)
    io_b <= _T