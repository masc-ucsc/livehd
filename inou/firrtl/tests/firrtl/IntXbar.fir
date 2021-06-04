circuit IntXbar: 
  module IntXbar : 
    input clock : Clock
    input reset : UInt<1>
    output auto : {flip int_in : UInt<1>[1], int_out : UInt<1>[1]}
    
    clock is invalid
    reset is invalid
    auto is invalid
    wire _T : UInt<1>[1] @[Nodes.scala 389:84]
    _T is invalid @[Nodes.scala 389:84]
    wire _T_1 : UInt<1>[1] @[Nodes.scala 388:84]
    _T_1 is invalid @[Nodes.scala 388:84]
    auto.int_out <- _T_1 @[LazyModule.scala 181:49]
    _T <- auto.int_in @[LazyModule.scala 181:31]
    _T_1[0] <= _T[0] @[Xbar.scala 21:44]
