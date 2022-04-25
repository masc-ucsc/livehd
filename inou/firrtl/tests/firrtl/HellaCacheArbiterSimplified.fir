circuit HellaCacheArbiterSimplified :
  module HellaCacheArbiterSimplified : 
    input clock : Clock
    input reset : UInt<1>
;   output io : {flip requestor : {req : {flip ready : UInt<1>, valid : UInt<1>, bits : {addr : UInt<40>, tag : UInt<7>, cmd : UInt<5>, size : UInt<2>, signed : UInt<1>, dprv : UInt<2>, phys : UInt<1>, no_alloc : UInt<1>, no_xcpt : UInt<1>, data : UInt<64>, mask : UInt<8>}}}[1], mem : {req : {flip ready : UInt<1>, valid : UInt<1>, bits : {addr : UInt<40>, tag : UInt<7>, cmd : UInt<5>, size : UInt<2>, signed : UInt<1>, dprv : UInt<2>, phys : UInt<1>, no_alloc : UInt<1>, no_xcpt : UInt<1>, data : UInt<64>, mask : UInt<8>}}}}
    output io : {flip requestor : {req : {flip ready : UInt<1>, foo : UInt<1>}}, mem : {req : {flip ready : UInt<1>, foo : UInt<1>}}}
    clock is invalid
    reset is invalid
    io is invalid
    io.mem <- io.requestor @[HellaCacheArbiter.scala 17:12]
