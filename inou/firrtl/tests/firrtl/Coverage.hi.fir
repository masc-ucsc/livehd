circuit Coverage :
  module Coverage :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip in1 : UInt<4>, flip in2 : UInt<4>, flip in3 : UInt<2>, out1 : UInt<1>, out2 : UInt<4>}

    node _T = neq(io.in1, UInt<1>("h0"))
    node _T_1 = andr(io.in2)
    node _T_2 = orr(io.in2)
    io.out1 <= mux(_T, _T_1, _T_2)
    node _T_3 = pad(io.in2, 4)
    node _T_4 = validif(_T, _T_3)
    io.out2 <= _T_4