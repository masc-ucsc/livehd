circuit Shifts :
  module Shifts :
    input clock : Clock
    input reset : UInt<1>
    input io_in1 : UInt<4>
    input io_in2 : UInt<4>
    input io_in3 : UInt<4>
    input io_in4 : UInt<1>
    output io_out1 : UInt<4>
    output io_out2 : UInt<5>
    output io_out3 : UInt<3>
    output io_out4 : UInt<5>
    output io_out5 : UInt<4>

    node _T = head(io_in1, 2)
    node _T_1 = head(io_in2, 2)
    node _T_2 = cat(_T, _T_1)
    node _T_3 = shl(io_in3, 1)
    node _T_4 = shr(io_in3, 1)
    node _T_5 = dshl(io_in3, io_in4)
    node _T_6 = dshr(io_in3, io_in4)
    io_out1 <= _T_2
    io_out2 <= _T_3
    io_out3 <= _T_4
    io_out4 <= _T_5
    io_out5 <= _T_6