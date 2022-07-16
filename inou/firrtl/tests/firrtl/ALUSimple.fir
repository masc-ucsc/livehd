circuit ALUSimple :
  module ALUSimple : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip operation : UInt<4>, flip inputx : UInt<32>, flip inputy : UInt<32>, result : UInt<32>}
    
    io.result <= UInt<1>("h00") 
    node _T = eq(io.operation, UInt<3>("h06")) 
    when _T : 
      node _T_1 = and(io.inputx, io.inputy) 
      io.result <= _T_1 
      skip 
    else : 
      node _T_2 = eq(io.operation, UInt<3>("h05")) 
      when _T_2 : 
        node _T_3 = or(io.inputx, io.inputy) 
        io.result <= _T_3 
        skip 
      else : 
        node _T_4 = eq(io.operation, UInt<3>("h07")) 
        when _T_4 : 
          node _T_5 = add(io.inputx, io.inputy) 
          node _T_6 = tail(_T_5, 1) 
          io.result <= _T_6 
          skip 
        else : 
          node _T_7 = eq(io.operation, UInt<3>("h04")) 
          when _T_7 : 
            node _T_8 = sub(io.inputx, io.inputy) 
            node _T_9 = tail(_T_8, 1) 
            io.result <= _T_9 
            skip 
          else : 
            node _T_10 = eq(io.operation, UInt<2>("h03")) 
            when _T_10 : 
              node _T_11 = asSInt(io.inputx) 
              node _T_12 = bits(io.inputy, 4, 0) 
              node _T_13 = dshr(_T_11, _T_12) 
              node _T_14 = asUInt(_T_13) 
              io.result <= _T_14 
              skip 
            else : 
              node _T_15 = eq(io.operation, UInt<1>("h01")) 
              when _T_15 : 
                node _T_16 = lt(io.inputx, io.inputy) 
                io.result <= _T_16 
                skip 
              else : 
                node _T_17 = eq(io.operation, UInt<1>("h00")) 
                when _T_17 : 
                  node _T_18 = xor(io.inputx, io.inputy) 
                  io.result <= _T_18 
                  skip 
                else : 
                  node _T_19 = eq(io.operation, UInt<2>("h02")) 
                  when _T_19 : 
                    node _T_20 = bits(io.inputy, 4, 0) 
                    node _T_21 = dshr(io.inputx, _T_20) 
                    io.result <= _T_21 
                    skip 
                  else : 
                    node _T_22 = eq(io.operation, UInt<4>("h09")) 
                    when _T_22 : 
                      node _T_23 = asSInt(io.inputx) 
                      node _T_24 = asSInt(io.inputy) 
                      node _T_25 = lt(_T_23, _T_24) 
                      io.result <= _T_25 
                      skip 
                    else : 
                      node _T_26 = eq(io.operation, UInt<4>("h08")) 
                      when _T_26 : 
                        node _T_27 = bits(io.inputy, 4, 0) 
                        node _T_28 = dshl(io.inputx, _T_27) 
                        io.result <= _T_28 
                        skip 
                      else : 
                        node _T_29 = eq(io.operation, UInt<4>("h0a")) 
                        when _T_29 : 
                          node _T_30 = or(io.inputx, io.inputy) 
                          node _T_31 = not(_T_30) 
                          io.result <= _T_31 
                          skip 
                        else : 
                          node _T_32 = eq(io.operation, UInt<4>("h0b")) 
                          when _T_32 : 
                            node _T_33 = asSInt(io.inputx) 
                            node _T_34 = asSInt(io.inputy) 
                            node _T_35 = geq(_T_33, _T_34) 
                            io.result <= _T_35 
                            skip 
                          else : 
                            node _T_36 = eq(io.operation, UInt<4>("h0c")) 
                            when _T_36 : 
                              node _T_37 = geq(io.inputx, io.inputy) 
                              io.result <= _T_37 
                              skip 
                            else : 
                              node _T_38 = eq(io.operation, UInt<4>("h0d")) 
                              when _T_38 : 
                                node _T_39 = eq(io.inputx, io.inputy) 
                                io.result <= _T_39 
                                skip 
    
