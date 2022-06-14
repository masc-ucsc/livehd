;buildInfoPackage: chisel3, version: 3.4.2, scalaVersion: 2.12.12, sbtVersion: 1.3.10
circuit SmemStruct : 
  module SmemStruct : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip en : UInt<1>, flip write : UInt<1>, flip addr : UInt<10>, flip din : {aa : {d1 : UInt<2>, d2 : UInt<2>}, bb : UInt<4>}, dout : {aa : {d1 : UInt<2>, d2 : UInt<2>}, bb : UInt<4>}}
    
    smem mem : {aa : {d1 : UInt<2>, d2 : UInt<2>}, bb : UInt<4>}[1024], undefined 
    infer mport mportw = mem[io.addr], clock
    mportw.bb <= io.din.bb
    mportw.aa.d2 <= io.din.aa.d2
    mportw.aa.d1 <= io.din.aa.d1
    wire rd_en : UInt<1> 
    node _T = gt(io.addr, UInt<6>("h020")) 

    when _T : 
      rd_en <= io.en 
      skip 
    else : 
      rd_en <= UInt<1>("h01") 
      skip 

    wire _T_1 : UInt 
    _T_1 is invalid 
    when rd_en : 
      _T_1 <= io.addr 
      node _T_2 = or(_T_1, UInt<10>("h00")) 
      node _T_3 = bits(_T_2, 9, 0) 
      infer mport mportr = mem[_T_3], clock 
      skip 
    io.dout.bb <= mportr.bb 
    io.dout.aa.d2 <= mportr.aa.d2 
    io.dout.aa.d1 <= mportr.aa.d1 
    
