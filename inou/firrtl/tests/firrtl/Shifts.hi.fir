circuit Shifts :
  module Shifts :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip in1 : UInt<4>, flip in2 : UInt<4>, flip in3 : UInt<4>, flip in4 : UInt<1>, out1 : UInt<4>, out2 : UInt<5>, out3 : UInt<3>, out4 : UInt<5>, out5 : UInt<4>}

    node _T = head(io.in1, 2)
    node _T_1 = head(io.in2, 2)
    node _T_2 = cat(_T, _T_1)
    io.out1 <= _T_2
    node _T_3 = shl(io.in3, 1)
    io.out2 <= _T_3
    node _T_4 = shr(io.in3, 1)
    io.out3 <= _T_4
    node _T_5 = dshl(io.in3, io.in4)
    io.out4 <= _T_5
    node _T_6 = dshr(io.in3, io.in4)
    io.out5 <= _T_6