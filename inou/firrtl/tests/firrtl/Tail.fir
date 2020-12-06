circuit Tail :
  module Tail :
    output io : { flip a : UInt<3>, b : UInt<1>}

    node _T = tail(io.a, 2)
    io.b <= _T