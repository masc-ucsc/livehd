circuit NextPC :
  module NextPC : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip branch : UInt<1>, flip jal : UInt<1>, flip jalr : UInt<1>, flip inputx : UInt<32>, flip inputy : UInt<32>, flip funct3 : UInt<3>, flip pc : UInt<32>, flip imm : UInt<32>, nextpc : UInt<32>, taken : UInt<1>}
    
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
          else : 
            node _T_8 = eq(io.funct3, UInt<3>("h05")) 
            when _T_8 : 
              node _T_9 = asSInt(io.inputx) 
              node _T_10 = asSInt(io.inputy) 
              node _T_11 = geq(_T_9, _T_10) 
              io.taken <= _T_11 
              skip 
            else : 
              node _T_12 = eq(io.funct3, UInt<3>("h06")) 
              when _T_12 : 
                node _T_13 = lt(io.inputx, io.inputy) 
                io.taken <= _T_13 
                skip 
              else : 
                node _T_14 = eq(io.funct3, UInt<3>("h07")) 
                when _T_14 : 
                  node _T_15 = geq(io.inputx, io.inputy) 
                  io.taken <= _T_15 
                  skip 
                else : 
                  io.taken <= UInt<1>("h00") 
                  skip 
      when io.taken : 
        node _T_16 = add(io.pc, io.imm) 
        node _T_17 = tail(_T_16, 1) 
        io.nextpc <= _T_17 
        skip 
      else : 
        node _T_18 = add(io.pc, UInt<3>("h04")) 
        node _T_19 = tail(_T_18, 1) 
        io.nextpc <= _T_19 
        skip 
      skip 
    else : 
      when io.jal : 
        io.taken <= UInt<1>("h01") 
        node _T_20 = add(io.pc, io.imm) 
        node _T_21 = tail(_T_20, 1) 
        io.nextpc <= _T_21 
        skip 
      else : 
        when io.jalr : 
          io.taken <= UInt<1>("h01") 
          node _T_22 = add(io.inputx, io.imm) 
          node _T_23 = tail(_T_22, 1) 
          io.nextpc <= _T_23 
          skip 
        else : 
          node _T_24 = add(io.pc, UInt<3>("h04")) 
          node _T_25 = tail(_T_24, 1) 
          io.nextpc <= _T_25 
          io.taken <= UInt<1>("h00") 
          skip 
    
