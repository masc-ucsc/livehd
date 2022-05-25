circuit NotAnd : 
  module NotAnd :
    input io_a : UInt<4>
    input io_b : UInt<4>
    output io_x : UInt<4>

    node _T = and(not(io_a), io_b)
    io_x <= _T
