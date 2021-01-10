circuit BundleConnect :
  module BundleConnectSubMod :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip in1 : UInt<4>, flip in2 : UInt<4>, out : UInt<4>}

    node _io_out_T = add(io.in1, io.in2) @[BundleConnect.scala 27:20]
    node _io_out_T_1 = tail(_io_out_T, 1) @[BundleConnect.scala 27:20]
    io.out <= _io_out_T_1 @[BundleConnect.scala 27:10]

  module BundleConnect :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip in1 : UInt<4>, flip in2 : UInt<4>, out : UInt<4>}

    inst submodule_inst of BundleConnectSubMod @[BundleConnect.scala 16:30]
    submodule_inst.clock <= clock
    submodule_inst.reset <= reset
    io.out <= submodule_inst.io.out @[BundleConnect.scala 17:6]
    submodule_inst.io.in2 <= io.in2 @[BundleConnect.scala 17:6]
    submodule_inst.io.in1 <= io.in1 @[BundleConnect.scala 17:6]