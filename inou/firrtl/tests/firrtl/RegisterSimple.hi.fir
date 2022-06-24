circuit RegisterSimple :
  module RegisterSimple :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip inVal : UInt<16>, outVal : UInt<16>}

    reg x : UInt<16>, clock with :
      reset => (UInt<1>("h0"), x) 
    node _T = eq(x, UInt<1>("h0")) 
    when _T : 
      x <= io.inVal 
    else :
      node _x_T = sub(x, UInt<1>("h1")) 
      node _x_T_1 = tail(_x_T, 1) 
      x <= _x_T_1 
    io.outVal <= x 