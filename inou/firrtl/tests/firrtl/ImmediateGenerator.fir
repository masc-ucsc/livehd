circuit ImmediateGenerator :
  module ImmediateGenerator : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip instruction : UInt<32>, sextImm : UInt<32>}
    
    io.sextImm <= UInt<1>("h00") 
    node opcode = bits(io.instruction, 6, 0) 
    node _T = eq(UInt<6>("h037"), opcode) 
    when _T : 
      node _T_1 = bits(io.instruction, 31, 12) 
      node _T_2 = mux(UInt<1>("h00"), UInt<12>("h0fff"), UInt<12>("h00")) 
      node _T_3 = cat(_T_1, _T_2) 
      io.sextImm <= _T_3 
      skip 
    else : 
      node _T_4 = eq(UInt<5>("h017"), opcode) 
      when _T_4 : 
        node _T_5 = bits(io.instruction, 31, 12) 
        node _T_6 = mux(UInt<1>("h00"), UInt<12>("h0fff"), UInt<12>("h00")) 
        node _T_7 = cat(_T_5, _T_6) 
        io.sextImm <= _T_7 
        skip 
      else : 
        node _T_8 = eq(UInt<7>("h06f"), opcode) 
        when _T_8 : 
          node _T_9 = bits(io.instruction, 31, 31) 
          node _T_10 = bits(io.instruction, 19, 12) 
          node _T_11 = bits(io.instruction, 20, 20) 
          node _T_12 = bits(io.instruction, 30, 21) 
          node _T_13 = cat(_T_11, _T_12) 
          node _T_14 = cat(_T_9, _T_10) 
          node _T_15 = cat(_T_14, _T_13) 
          node _T_16 = bits(_T_15, 19, 19) 
          node _T_17 = bits(_T_16, 0, 0) 
          node _T_18 = mux(_T_17, UInt<11>("h07ff"), UInt<11>("h00")) 
          node _T_19 = cat(_T_18, _T_15) 
          node _T_20 = cat(_T_19, UInt<1>("h00")) 
          io.sextImm <= _T_20 
          skip 
        else : 
          node _T_21 = eq(UInt<7>("h067"), opcode) 
          when _T_21 : 
            node _T_22 = bits(io.instruction, 31, 20) 
            node _T_23 = bits(_T_22, 11, 11) 
            node _T_24 = bits(_T_23, 0, 0) 
            node _T_25 = mux(_T_24, UInt<20>("h0fffff"), UInt<20>("h00")) 
            node _T_26 = cat(_T_25, _T_22) 
            io.sextImm <= _T_26 
            skip 
          else : 
            node _T_27 = eq(UInt<7>("h063"), opcode) 
            when _T_27 : 
              node _T_28 = bits(io.instruction, 31, 31) 
              node _T_29 = bits(io.instruction, 7, 7) 
              node _T_30 = bits(io.instruction, 30, 25) 
              node _T_31 = bits(io.instruction, 11, 8) 
              node _T_32 = cat(_T_30, _T_31) 
              node _T_33 = cat(_T_28, _T_29) 
              node _T_34 = cat(_T_33, _T_32) 
              node _T_35 = bits(_T_34, 11, 11) 
              node _T_36 = bits(_T_35, 0, 0) 
              node _T_37 = mux(_T_36, UInt<19>("h07ffff"), UInt<19>("h00")) 
              node _T_38 = cat(_T_37, _T_34) 
              node _T_39 = cat(_T_38, UInt<1>("h00")) 
              io.sextImm <= _T_39 
              skip 
            else : 
              node _T_40 = eq(UInt<2>("h03"), opcode) 
              when _T_40 : 
                node _T_41 = bits(io.instruction, 31, 20) 
                node _T_42 = bits(_T_41, 11, 11) 
                node _T_43 = bits(_T_42, 0, 0) 
                node _T_44 = mux(_T_43, UInt<20>("h0fffff"), UInt<20>("h00")) 
                node _T_45 = cat(_T_44, _T_41) 
                io.sextImm <= _T_45 
                skip 
              else : 
                node _T_46 = eq(UInt<6>("h023"), opcode) 
                when _T_46 : 
                  node _T_47 = bits(io.instruction, 31, 25) 
                  node _T_48 = bits(io.instruction, 11, 7) 
                  node _T_49 = cat(_T_47, _T_48) 
                  node _T_50 = bits(_T_49, 11, 11) 
                  node _T_51 = bits(_T_50, 0, 0) 
                  node _T_52 = mux(_T_51, UInt<20>("h0fffff"), UInt<20>("h00")) 
                  node _T_53 = cat(_T_52, _T_49) 
                  io.sextImm <= _T_53 
                  skip 
                else : 
                  node _T_54 = eq(UInt<5>("h013"), opcode) 
                  when _T_54 : 
                    node _T_55 = bits(io.instruction, 31, 20) 
                    node _T_56 = bits(_T_55, 11, 11) 
                    node _T_57 = bits(_T_56, 0, 0) 
                    node _T_58 = mux(_T_57, UInt<20>("h0fffff"), UInt<20>("h00")) 
                    node _T_59 = cat(_T_58, _T_55) 
                    io.sextImm <= _T_59 
                    skip 
                  else : 
                    node _T_60 = eq(UInt<7>("h073"), opcode) 
                    when _T_60 : 
                      node _T_61 = mux(UInt<1>("h00"), UInt<27>("h07ffffff"), UInt<27>("h00")) 
                      node _T_62 = bits(io.instruction, 19, 15) 
                      node _T_63 = cat(_T_61, _T_62) 
                      io.sextImm <= _T_63 
                      skip 
    
