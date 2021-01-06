circuit BundleConnect :
  module BundleConnectSubMod :
    input clock : Clock
    input reset : UInt<1>
    input io_in1 : UInt<4>
    input io_in2 : UInt<4>
    output io_out : UInt<4>

    node _io_out_T = add(io_in1, io_in2) @[BundleConnect.scala 27:20]
    node _io_out_T_1 = tail(_io_out_T, 1) @[BundleConnect.scala 27:20]
    io_out <= _io_out_T_1 @[BundleConnect.scala 27:10]

  module BundleConnect :
    input clock : Clock
    input reset : UInt<1>
    input io_in1 : UInt<4>
    input io_in2 : UInt<4>
    output io_out : UInt<4>

    inst submodule_inst of BundleConnectSubMod @[BundleConnect.scala 16:30]
    io_out <= submodule_inst.io_out @[BundleConnect.scala 17:6]
    submodule_inst.clock <= clock
    submodule_inst.reset <= reset
    submodule_inst.io_in1 <= io_in1 @[BundleConnect.scala 17:6]
    submodule_inst.io_in2 <= io_in2 @[BundleConnect.scala 17:6]