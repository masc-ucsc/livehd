circuit ICombinMemPort :
  module ICombinMemPort : 
    input clock : Clock
    input reset : UInt<1>
    output io : {pipeline : {flip address : UInt<32>, flip valid : UInt<1>, good : UInt<1>, instruction : UInt<32>, ready : UInt<1>}, flip bus : {flip request : {flip ready : UInt<1>, valid : UInt<1>, bits : {address : UInt<32>, writedata : UInt<32>, operation : UInt<2>}}, response : {valid : UInt<1>, bits : {data : UInt<32>}}}}
    
    wire _T : {flip address : UInt<32>, flip valid : UInt<1>, good : UInt<1>, instruction : UInt<32>, ready : UInt<1>} 
    _T.ready <= UInt<1>("h00") 
    _T.instruction <= UInt<32>("h00") 
    _T.good <= UInt<1>("h00") 
    _T.valid <= UInt<1>("h00") 
    _T.address <= UInt<32>("h00") 
    io.pipeline.ready <= _T.ready 
    io.pipeline.instruction <= _T.instruction 
    io.pipeline.good <= _T.good 
    _T.valid <= io.pipeline.valid 
    _T.address <= io.pipeline.address 
    io.bus.response.bits.data is invalid 
    io.bus.response.valid is invalid 
    io.bus.request.bits.operation is invalid 
    io.bus.request.bits.writedata is invalid 
    io.bus.request.bits.address is invalid 
    io.bus.request.valid is invalid 
    io.bus.request.ready is invalid 
    when io.pipeline.valid : 
      wire _T_1 : {address : UInt<32>, writedata : UInt<32>, operation : UInt<2>} 
      _T_1.address <= io.pipeline.address 
      _T_1.operation <= UInt<1>("h00") 
      _T_1.writedata <= UInt<1>("h00") 
      io.bus.request.bits.operation <= _T_1.operation 
      io.bus.request.bits.writedata <= _T_1.writedata 
      io.bus.request.bits.address <= _T_1.address 
      io.bus.request.valid <= UInt<1>("h01") 
      skip 
    else : 
      io.bus.request.valid <= UInt<1>("h00") 
      skip 
    io.pipeline.ready <= UInt<1>("h01") 
    io.pipeline.good <= UInt<1>("h01") 
    io.pipeline.instruction <= io.bus.response.bits.data 
    
