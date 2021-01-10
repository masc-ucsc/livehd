circuit Coverage :
  module Coverage :
    input clock : Clock
    input reset : UInt<1>
    input io_in1 : UInt<4>
    input io_in2 : UInt<4>
    input io_in3 : UInt<2>
    output io_out1 : UInt<1>
    output io_out2 : UInt<4>

    node _T = neq(io_in1, UInt<1>("h0"))
    node _T_1 = andr(io_in2)
    node _T_2 = orr(io_in2)
    node _T_3 = pad(io_in2, 4)
    node _T_4 = validif(_T, _T_3)
    io_out1 <= mux(_T, _T_1, _T_2)
    io_out2 <= _T_4