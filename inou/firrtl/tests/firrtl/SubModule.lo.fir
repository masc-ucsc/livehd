circuit SubModule :
  module SubModuleSubMod :
    input clock : Clock
    input reset : UInt<1>
    input io_a : UInt<4>
    output io_b : UInt<4>

    node _io_b_T = add(io_a, UInt<1>("h1")) @[SubModule.scala 29:16]
    node _io_b_T_1 = tail(_io_b_T, 1) @[SubModule.scala 29:16]
    io_b <= _io_b_T_1 @[SubModule.scala 29:8]

  module SubModule :
    input clock : Clock
    input reset : UInt<1>
    input io_inp : UInt<4>
    output io_out : UInt<4>

    inst submodule_inst of SubModuleSubMod @[SubModule.scala 18:30]
    io_out <= submodule_inst.io_b @[SubModule.scala 20:10]
    submodule_inst.clock <= clock
    submodule_inst.reset <= reset
    submodule_inst.io_a <= io_inp @[SubModule.scala 19:23]