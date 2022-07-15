circuit ForwardingUnit :
  module ForwardingUnit : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip rs1 : UInt<5>, flip rs2 : UInt<5>, flip exmemrd : UInt<5>, flip exmemrw : UInt<1>, flip memwbrd : UInt<5>, flip memwbrw : UInt<1>, forwardA : UInt<2>, forwardB : UInt<2>}
    
    node _T = eq(io.exmemrw, UInt<1>("h01")) 
    node _T_1 = eq(io.exmemrd, io.rs1) 
    node _T_2 = and(_T, _T_1) 
    node _T_3 = neq(io.exmemrd, UInt<1>("h00")) 
    node _T_4 = and(_T_2, _T_3) 
    when _T_4 : 
      io.forwardA <= UInt<1>("h01") 
      skip 
    else : 
      node _T_5 = eq(io.memwbrw, UInt<1>("h01")) 
      node _T_6 = eq(io.memwbrd, io.rs1) 
      node _T_7 = and(_T_5, _T_6) 
      node _T_8 = neq(io.memwbrd, UInt<1>("h00")) 
      node _T_9 = and(_T_7, _T_8) 
      when _T_9 : 
        io.forwardA <= UInt<2>("h02") 
        skip 
      else : 
        io.forwardA <= UInt<1>("h00") 
        skip 
    node _T_10 = eq(io.exmemrw, UInt<1>("h01")) 
    node _T_11 = eq(io.exmemrd, io.rs2) 
    node _T_12 = and(_T_10, _T_11) 
    node _T_13 = neq(io.exmemrd, UInt<1>("h00")) 
    node _T_14 = and(_T_12, _T_13) 
    when _T_14 : 
      io.forwardB <= UInt<1>("h01") 
      skip 
    else : 
      node _T_15 = eq(io.memwbrw, UInt<1>("h01")) 
      node _T_16 = eq(io.memwbrd, io.rs2) 
      node _T_17 = and(_T_15, _T_16) 
      node _T_18 = neq(io.memwbrd, UInt<1>("h00")) 
      node _T_19 = and(_T_17, _T_18) 
      when _T_19 : 
        io.forwardB <= UInt<2>("h02") 
        skip 
      else : 
        io.forwardB <= UInt<1>("h00") 
        skip 
    
