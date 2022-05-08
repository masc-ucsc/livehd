circuit BreakpointUnit_4 :
  module BreakpointUnit_4 : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip status : {debug : UInt<1>, cease : UInt<1>, wfi : UInt<1>, isa : UInt<32>, dprv : UInt<2>, prv : UInt<2>, sd : UInt<1>, zero2 : UInt<27>, sxl : UInt<2>, uxl : UInt<2>, sd_rv32 : UInt<1>, zero1 : UInt<8>, tsr : UInt<1>, tw : UInt<1>, tvm : UInt<1>, mxr : UInt<1>, sum : UInt<1>, mprv : UInt<1>, xs : UInt<2>, fs : UInt<2>, mpp : UInt<2>, vs : UInt<2>, spp : UInt<1>, mpie : UInt<1>, hpie : UInt<1>, spie : UInt<1>, upie : UInt<1>, mie : UInt<1>, hie : UInt<1>, sie : UInt<1>, uie : UInt<1>}, flip bp : {control : {ttype : UInt<4>, dmode : UInt<1>, maskmax : UInt<6>, reserved : UInt<40>, action : UInt<1>, chain : UInt<1>, zero : UInt<2>, tmatch : UInt<2>, m : UInt<1>, h : UInt<1>, s : UInt<1>, u : UInt<1>, x : UInt<1>, w : UInt<1>, r : UInt<1>}, address : UInt<39>}[0], flip pc : UInt<39>, flip ea : UInt<39>, xcpt_if : UInt<1>, xcpt_ld : UInt<1>, xcpt_st : UInt<1>, debug_if : UInt<1>, debug_ld : UInt<1>, debug_st : UInt<1>, bpwatch : {valid : UInt<1>[1], rvalid : UInt<1>[1], wvalid : UInt<1>[1], ivalid : UInt<1>[1], action : UInt<3>}[0]}
    
    io.xcpt_if <= UInt<1>("h00") @[Breakpoint.scala 74:14]
    io.xcpt_ld <= UInt<1>("h00") @[Breakpoint.scala 75:14]
    io.xcpt_st <= UInt<1>("h00") @[Breakpoint.scala 76:14]
    io.debug_if <= UInt<1>("h00") @[Breakpoint.scala 77:15]
    io.debug_ld <= UInt<1>("h00") @[Breakpoint.scala 78:15]
    io.debug_st <= UInt<1>("h00") @[Breakpoint.scala 79:15]
