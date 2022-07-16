circuit NextPCSimple :
  module NextPCSimple : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip branch : UInt<1>, flip jal : UInt<1>, flip jalr : UInt<1>, flip inputx : UInt<32>, flip inputy : UInt<32>, flip funct3 : UInt<3>, flip pc : UInt<32>, flip imm : UInt<32>, nextpc : UInt<32>, taken : UInt<1>}
    io.taken <= UInt<1>("h0")
    io.nextpc <= UInt<32>("h8")
    when io.branch : 
      node _T = eq(io.funct3, UInt<1>("h00")) 
      when _T : 
        node _T_1 = eq(io.inputx, io.inputy) 
        io.taken <= _T_1 
        skip 
      else : 
        node _T_2 = eq(io.funct3, UInt<1>("h01")) 
        when _T_2 : 
          node _T_3 = neq(io.inputx, io.inputy) 
          io.taken <= _T_3 
          skip 
        else : 
          node _T_4 = eq(io.funct3, UInt<3>("h04")) 
          when _T_4 : 
            node _T_5 = asSInt(io.inputx) 
            node _T_6 = asSInt(io.inputy) 
            node _T_7 = lt(_T_5, _T_6) 
            io.taken <= _T_7 
            skip 
