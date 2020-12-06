circuit SubModule :
  module SubModuleSubMod :
    input clock : Clock
    input reset : Reset
    output io : { flip a : UInt<4>, b : UInt<4>}

    node _T = add(io.a, UInt<1>("h1")) @[SubModule.scala 29:16]
    node _T_1 = tail(_T, 1) @[SubModule.scala 29:16]
    io.b <= _T_1 @[SubModule.scala 29:8]

  module SubModule :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip inp : UInt<4>, out : UInt<4>}

    inst submodule_inst of SubModuleSubMod @[SubModule.scala 18:30]
    submodule_inst.clock <= clock
    submodule_inst.reset <= reset
    submodule_inst.io.a <= io.inp @[SubModule.scala 19:23]
    io.out <= submodule_inst.io.b @[SubModule.scala 20:10]