circuit ClockGroupAggregator :
  module ClockGroupAggregator : 
    output auto : {flip in : {member : {5 : {clock : Clock, reset : UInt<1>}, 4 : {clock : Clock, reset : UInt<1>}, 3 : {clock : Clock, reset : UInt<1>}, 2 : {clock : Clock, reset : UInt<1>}, 1 : {clock : Clock, reset : UInt<1>}, 0 : {clock : Clock, reset : UInt<1>}}}, 
                     out_3 : {member : {1 : {clock : Clock, reset : UInt<1>}, 0 : {clock : Clock, reset : UInt<1>}}}, 
                     out_2 : {member : {0 : {clock : Clock, reset : UInt<1>}}}, 
                     out_1 : {member : {1 : {clock : Clock, reset : UInt<1>}, 0 : {clock : Clock, reset : UInt<1>}}}, 
                     out_0 : {member : {0 : {clock : Clock, reset : UInt<1>}}}}
    
    auto is invalid
    wire childClock : Clock @[LazyModule.scala 201:24]
    childClock is invalid @[LazyModule.scala 201:24]
    wire childReset : Reset @[LazyModule.scala 202:24]
    childReset is invalid @[LazyModule.scala 202:24]
    node _T = asClock(UInt<1>("h00")) @[LazyModule.scala 203:29]
    childClock <= _T @[LazyModule.scala 203:14]
    childReset is invalid @[LazyModule.scala 204:14]
    wire in_0 : {member : {5 : {clock : Clock, reset : Reset}, 4 : {clock : Clock, reset : Reset}, 3 : {clock : Clock, reset : Reset}, 2 : {clock : Clock, reset : Reset}, 1 : {clock : Clock, reset : Reset}, 0 : {clock : Clock, reset : Reset}}} @[Nodes.scala 389:84]
    in_0 is invalid @[Nodes.scala 389:84]
    wire out_0 : {member : {0 : {clock : Clock, reset : Reset}}} @[Nodes.scala 388:84]
    out_0 is invalid @[Nodes.scala 388:84]
    wire out_1 : {member : {1 : {clock : Clock, reset : Reset}, 0 : {clock : Clock, reset : Reset}}} @[Nodes.scala 388:84]
    out_1 is invalid @[Nodes.scala 388:84]
    wire out_2 : {member : {0 : {clock : Clock, reset : Reset}}} @[Nodes.scala 388:84]
    out_2 is invalid @[Nodes.scala 388:84]
    wire out_3 : {member : {1 : {clock : Clock, reset : Reset}, 0 : {clock : Clock, reset : Reset}}} @[Nodes.scala 388:84]
    out_3 is invalid @[Nodes.scala 388:84]
    auto.out_0 <- out_0 @[LazyModule.scala 181:49]
    auto.out_1 <- out_1 @[LazyModule.scala 181:49]
    auto.out_2 <- out_2 @[LazyModule.scala 181:49]
    auto.out_3 <- out_3 @[LazyModule.scala 181:49]
    in_0 <- auto.in @[LazyModule.scala 181:31]
    out_0.member.0.reset <= in_0.member.0.reset @[ClockGroup.scala 50:60]
    out_0.member.0.clock <= in_0.member.0.clock @[ClockGroup.scala 50:60]
    out_1.member.0.reset <= in_0.member.1.reset @[ClockGroup.scala 50:60]
    out_1.member.0.clock <= in_0.member.1.clock @[ClockGroup.scala 50:60]
    out_1.member.1.reset <= in_0.member.2.reset @[ClockGroup.scala 50:60]
    out_1.member.1.clock <= in_0.member.2.clock @[ClockGroup.scala 50:60]
    out_2.member.0.reset <= in_0.member.3.reset @[ClockGroup.scala 50:60]
    out_2.member.0.clock <= in_0.member.3.clock @[ClockGroup.scala 50:60]
    out_3.member.0.reset <= in_0.member.4.reset @[ClockGroup.scala 50:60]
    out_3.member.0.clock <= in_0.member.4.clock @[ClockGroup.scala 50:60]
    out_3.member.1.reset <= in_0.member.5.reset @[ClockGroup.scala 50:60]
    out_3.member.1.clock <= in_0.member.5.clock @[ClockGroup.scala 50:60]
