circuit IntSyncSyncCrossingSink :
  module IntSyncSyncCrossingSink : 
    input clock : Clock
    input reset : UInt<1>
    output auto : {flip in : {sync : UInt<1>[2]}, out : UInt<1>[2]}
    
    clock is invalid
    reset is invalid
    auto is invalid
    wire _T : {sync : UInt<1>[2]} @[Nodes.scala 389:84]
    _T is invalid @[Nodes.scala 389:84]
    wire _T_foo : UInt<1>[2] @[Nodes.scala 388:84]
    _T_foo is invalid @[Nodes.scala 388:84]
    auto.out <- _T_foo @[LazyModule.scala 181:49]
    _T <- auto.in @[LazyModule.scala 181:31]
    _T_foo <- _T.sync @[Crossing.scala 85:11]
