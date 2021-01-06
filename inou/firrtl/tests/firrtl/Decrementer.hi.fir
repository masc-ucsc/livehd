circuit Decrementer :
  module Decrementer :
    input clock : Clock
    input reset : UInt<1>
    input cond : UInt<1>
    input inp : UInt<16>
    output out : UInt<16>

    reg x : UInt<16>, clock with :
      reset => (UInt<1>("h0"), x)
    node _T = gt(inp, x)
    node _T_1 = sub(x, UInt<1>("h1"))
    node _T_2 = tail(_T_1, 1)
    node _T_3 = mux(_T, x, _T_2)
    node _T_4 = mux(cond, inp, _T_3)
    out <= x
    x <= _T_4