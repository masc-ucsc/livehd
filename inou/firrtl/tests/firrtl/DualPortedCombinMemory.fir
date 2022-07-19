circuit DualPortedCombinMemory :
  module DualPortedCombinMemory : 
    input clock : Clock
    input reset : UInt<1>
    output io : {imem : {flip request : {flip ready : UInt<1>, valid : UInt<1>, bits : {address : UInt<32>, writedata : UInt<32>, operation : UInt<2>}}, response : {valid : UInt<1>, bits : {data : UInt<32>}}}, dmem : {flip request : {flip ready : UInt<1>, valid : UInt<1>, bits : {address : UInt<32>, writedata : UInt<32>, operation : UInt<2>}}, response : {valid : UInt<1>, bits : {data : UInt<32>}}}}
    
    io.imem.request.bits.operation is invalid 
    io.imem.request.bits.writedata is invalid 
    io.imem.request.bits.address is invalid 
    io.imem.request.valid is invalid 
    io.imem.request.ready is invalid 
    io.dmem.request.bits.operation is invalid 
    io.dmem.request.bits.writedata is invalid 
    io.dmem.request.bits.address is invalid 
    io.dmem.request.valid is invalid 
    io.dmem.request.ready is invalid 
    wire _T : {valid : UInt<1>, bits : {data : UInt<32>}} 
    _T.bits.data <= UInt<32>("h00") 
    _T.valid <= UInt<1>("h00") 
    io.imem.response.bits.data <= _T.bits.data 
    io.imem.response.valid <= _T.valid 
    wire _T_1 : {valid : UInt<1>, bits : {data : UInt<32>}} 
    _T_1.bits.data <= UInt<32>("h00") 
    _T_1.valid <= UInt<1>("h00") 
    io.dmem.response.bits.data <= _T_1.bits.data 
    io.dmem.response.valid <= _T_1.valid 
    cmem memory : UInt<32>[16384] 
    io.imem.response.valid <= UInt<1>("h00") 
    io.imem.request.ready <= UInt<1>("h01") 
    when io.imem.request.valid : 
      node _T_2 = eq(io.imem.request.bits.operation, UInt<1>("h00")) 
      node _T_3 = asUInt(reset) 
      node _T_4 = or(_T_2, _T_3) 
      node _T_5 = eq(_T_4, UInt<1>("h00")) 
      when _T_5 : 
        stop(clock, UInt<1>(1), 1) 
        skip 
      node _T_6 = lt(io.imem.request.bits.address, UInt<17>("h010000")) 
      when _T_6 : 
        io.imem.response.valid <= UInt<1>("h01") 
        node _T_7 = shr(io.imem.request.bits.address, 2) 
        node _T_8 = bits(_T_7, 13, 0) 
        infer mport _T_9 = memory[_T_8], clock 
        io.imem.response.bits.data <= _T_9 
        skip 
      else : 
        io.imem.response.valid <= UInt<1>("h00") 
        skip 
      skip 
    else : 
      io.imem.response.valid <= UInt<1>("h00") 
      skip 
    io.dmem.response.valid <= UInt<1>("h00") 
    io.dmem.request.ready <= UInt<1>("h01") 
    when io.dmem.request.valid : 
      node _T_10 = neq(io.dmem.request.bits.operation, UInt<1>("h01")) 
      node _T_11 = asUInt(reset) 
      node _T_12 = or(_T_10, _T_11) 
      node _T_13 = eq(_T_12, UInt<1>("h00")) 
      when _T_13 : 
        stop(clock, UInt<1>(1), 1) 
        skip 
      node _T_14 = lt(io.dmem.request.bits.address, UInt<17>("h010000")) 
      node _T_15 = asUInt(reset) 
      node _T_16 = or(_T_14, _T_15) 
      node _T_17 = eq(_T_16, UInt<1>("h00")) 
      when _T_17 : 
        stop(clock, UInt<1>(1), 1) 
        skip 
      node _T_18 = shr(io.dmem.request.bits.address, 2) 
      node _T_19 = bits(_T_18, 13, 0) 
      read mport _T_20 = memory[_T_19], clock 
      io.dmem.response.bits.data <= _T_20 
      io.dmem.response.valid <= UInt<1>("h01") 
      node _T_21 = eq(io.dmem.request.bits.operation, UInt<2>("h02")) 
      when _T_21 : 
        node _T_22 = shr(io.dmem.request.bits.address, 2) 
        node _T_23 = bits(_T_22, 13, 0) 
        infer mport _T_24 = memory[_T_23], clock 
        _T_24 <= io.dmem.request.bits.writedata 
        skip 
      skip 
    else : 
      io.dmem.response.valid <= UInt<1>("h00") 
      skip 
    
