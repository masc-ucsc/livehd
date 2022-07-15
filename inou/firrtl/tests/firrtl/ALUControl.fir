circuit ALUControl :
  module ALUControl : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip aluop : UInt<1>, flip itype : UInt<1>, flip funct7 : UInt<7>, flip funct3 : UInt<3>, operation : UInt<4>}
    
    when io.aluop : 
      node _T = eq(io.funct3, UInt<1>("h00")) 
      when _T : 
        node _T_1 = eq(io.funct7, UInt<1>("h00")) 
        node _T_2 = or(io.itype, _T_1) 
        when _T_2 : 
          io.operation <= UInt<3>("h07") 
          skip 
        else : 
          io.operation <= UInt<3>("h04") 
          skip 
        skip 
      else : 
        node _T_3 = eq(io.funct3, UInt<1>("h01")) 
        when _T_3 : 
          io.operation <= UInt<4>("h08") 
          skip 
        else : 
          node _T_4 = eq(io.funct3, UInt<2>("h02")) 
          when _T_4 : 
            io.operation <= UInt<4>("h09") 
            skip 
          else : 
            node _T_5 = eq(io.funct3, UInt<2>("h03")) 
            when _T_5 : 
              io.operation <= UInt<1>("h01") 
              skip 
            else : 
              node _T_6 = eq(io.funct3, UInt<3>("h04")) 
              when _T_6 : 
                io.operation <= UInt<1>("h00") 
                skip 
              else : 
                node _T_7 = eq(io.funct3, UInt<3>("h05")) 
                when _T_7 : 
                  node _T_8 = eq(io.funct7, UInt<1>("h00")) 
                  when _T_8 : 
                    io.operation <= UInt<2>("h02") 
                    skip 
                  else : 
                    io.operation <= UInt<2>("h03") 
                    skip 
                  skip 
                else : 
                  node _T_9 = eq(io.funct3, UInt<3>("h06")) 
                  when _T_9 : 
                    io.operation <= UInt<3>("h05") 
                    skip 
                  else : 
                    io.operation <= UInt<3>("h06") 
                    skip 
      skip 
    else : 
      io.operation <= UInt<3>("h07") 
      skip 
    
