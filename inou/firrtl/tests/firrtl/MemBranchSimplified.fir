circuit MemBranchSimplified :
  module MemBranchSimplified :
    input clock : Clock
    input reset : UInt<1>
    output io : {flip enq : {bits : {data : UInt<1> }}, deq : {bits : {data : UInt<8>}}}
    
    clock is invalid
    reset is invalid
    io is invalid
    cmem ram : {data : UInt<8>}[1] 
    
    node value = UInt<1>("h00")
    node value_1 = UInt<1>("h00")


    infer mport _T_53 = ram[value], clock 
    _T_53.data <= io.enq.bits.data 

    wire out : {data : UInt<8>} 
    infer mport _T_64 = ram[value_1], clock 

    out.data <= _T_64.data 
    io.deq.bits.data <= out.data 

    node _T_100 = and(UInt<1>("h01"), UInt<1>("h01"))
    when _T_100 : 
      io.deq.bits.data <= io.enq.bits.data 
    
