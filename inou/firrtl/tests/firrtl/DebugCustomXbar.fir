circuit DebugCustomXbar :
  module DebugCustomXbar : 
    input clock : Clock
    input reset : UInt<1>
    output auto : {out : {flip addr : UInt<1>, data : UInt<0>, ready : UInt<1>, flip valid : UInt<1>}}
    
    clock is invalid
    reset is invalid
    auto is invalid
    wire sink : {flip addr : UInt<1>, data : UInt<0>, ready : UInt<1>, flip valid : UInt<1>} @[Nodes.scala 388:84]
    sink is invalid @[Nodes.scala 388:84]
    auto.out <- sink @[LazyModule.scala 181:49]
    sink.ready <= UInt<1>("h00") @[Custom.scala 83:16]
    sink.data <= UInt<1>("h00") @[Custom.scala 84:15]
