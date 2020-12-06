circuit Blackbox2 :
  extmodule BlackboxInline2 :
    input in1 : UInt<32>
    input in2 : UInt<32>
    output out : UInt<33>
    defname = BlackboxInline2
    parameter bw = 32

  module Blackbox2 :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip val0 : UInt<33>, flip val1 : UInt<32>, flip val2 : UInt<32>, valOut : UInt<33>}

    inst submodule_inst of BlackboxInline2 @[Blackbox2.scala 18:30]
    submodule_inst.out is invalid
    submodule_inst.in2 is invalid
    submodule_inst.in1 is invalid
    submodule_inst.in1 <= io.val1 @[Blackbox2.scala 19:25]
    submodule_inst.in2 <= io.val2 @[Blackbox2.scala 20:25]
    node _T = xor(submodule_inst.out, io.val0) @[Blackbox2.scala 21:38]
    io.valOut <= _T @[Blackbox2.scala 21:13]