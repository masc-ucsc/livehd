circuit HazardUnitBP :
  module HazardUnitBP : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip rs1 : UInt<5>, flip rs2 : UInt<5>, flip id_prediction : UInt<1>, flip idex_memread : UInt<1>, flip idex_rd : UInt<5>, flip exmem_taken : UInt<1>, pcSel : UInt<2>, if_id_stall : UInt<1>, if_id_flush : UInt<1>, id_ex_flush : UInt<1>, ex_mem_flush : UInt<1>}
    
    io.pcSel <= UInt<1>("h00") 
    io.if_id_stall <= UInt<1>("h00") 
    io.id_ex_flush <= UInt<1>("h00") 
    io.ex_mem_flush <= UInt<1>("h00") 
    io.if_id_flush <= UInt<1>("h00") 
    node _T = eq(io.exmem_taken, UInt<1>("h01")) 
    when _T : 
      io.pcSel <= UInt<1>("h01") 
      io.if_id_flush <= UInt<1>("h01") 
      io.id_ex_flush <= UInt<1>("h01") 
      io.ex_mem_flush <= UInt<1>("h01") 
      skip 
    else : 
      node _T_1 = eq(io.idex_memread, UInt<1>("h01")) 
      node _T_2 = eq(io.idex_rd, io.rs1) 
      node _T_3 = eq(io.idex_rd, io.rs2) 
      node _T_4 = or(_T_2, _T_3) 
      node _T_5 = and(_T_1, _T_4) 
      when _T_5 : 
        io.pcSel <= UInt<2>("h03") 
        io.if_id_stall <= UInt<1>("h01") 
        io.id_ex_flush <= UInt<1>("h01") 
        skip 
      else : 
        node _T_6 = eq(io.id_prediction, UInt<1>("h01")) 
        when _T_6 : 
          io.pcSel <= UInt<2>("h02") 
          io.if_id_flush <= UInt<1>("h01") 
          skip 
        else : 
          io.pcSel <= UInt<1>("h00") 
          io.if_id_stall <= UInt<1>("h00") 
          io.id_ex_flush <= UInt<1>("h00") 
          io.ex_mem_flush <= UInt<1>("h00") 
          io.if_id_flush <= UInt<1>("h00") 
          skip 
    
