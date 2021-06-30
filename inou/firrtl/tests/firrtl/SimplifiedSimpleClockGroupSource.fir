circuit SimplifiedSimpleClockGroupSource :
  module SimplifiedSimpleClockGroupSource : 
    input clock : Clock
    input reset : UInt<1>
    output auto : {out : {member : {0 : {clock : Clock, reset : Reset}}}}
    
    clock is invalid
    reset is invalid
    auto is invalid
    wire out_0 : {member : {0 : {clock : Clock, reset : Reset}}} @[Nodes.scala 388:84]
    out_0 is invalid @[Nodes.scala 388:84]
    auto.out <- out_0 @[LazyModule.scala 181:49]
    out_0.member.0.clock <= clock @[ClockGroup.scala 66:36]
    out_0.member.0.reset <= reset @[ClockGroup.scala 66:54]

