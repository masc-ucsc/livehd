circuit ClockGroup :
  module ClockGroup : 
    output auto : {flip in : {member : {0 : {clock : Clock, reset : UInt<1>}}}, out : {clock : Clock, reset : UInt<1>}}
    
    auto is invalid
    wire childClock : Clock @[LazyModule.scala 201:24]
    childClock is invalid @[LazyModule.scala 201:24]
    wire childReset : Reset @[LazyModule.scala 202:24]
    childReset is invalid @[LazyModule.scala 202:24]
    node _T = asClock(UInt<1>("h00")) @[LazyModule.scala 203:29]
    childClock <= _T @[LazyModule.scala 203:14]
    childReset is invalid @[LazyModule.scala 204:14]
    wire in : {member : {0 : {clock : Clock, reset : Reset}}} @[Nodes.scala 389:84]
    in is invalid @[Nodes.scala 389:84]
    wire out_0 : {clock : Clock, reset : Reset} @[Nodes.scala 388:84]
    out_0 is invalid @[Nodes.scala 388:84]
    auto.out <- out_0 @[LazyModule.scala 181:49]
    in <- auto.in @[LazyModule.scala 181:31]
    out_0.reset <= in.member.0.reset @[ClockGroup.scala 25:52]
    out_0.clock <= in.member.0.clock @[ClockGroup.scala 25:52]
