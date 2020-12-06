circuit Blackbox :
  extmodule BlackboxInline :
    input in1 : UInt<32>
    input in2 : UInt<32>
    output out : UInt<33>
    defname = BlackboxInline

  module Blackbox :
    input clock : Clock
    input reset : UInt<1>
    input io_val0 : UInt<33>
    input io_val1 : UInt<32>
    input io_val2 : UInt<32>
    output io_valOut : UInt<33>

    inst submodule_inst of BlackboxInline @[Blackbox.scala 18:30]
    node _io_valOut_T = xor(submodule_inst.out, io_val0) @[Blackbox.scala 21:38]
    io_valOut <= _io_valOut_T @[Blackbox.scala 21:13]
    submodule_inst.in1 <= io_val1 @[Blackbox.scala 19:25]
    submodule_inst.in2 <= io_val2 @[Blackbox.scala 20:25]