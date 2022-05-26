circuit SubModule :
  module SubModuleSubMod :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip a : UInt<4>, b : UInt<4>}

    node _io_b_T = add(io.a, UInt<1>("h1")) @[SubModule.scala 29:16]
    node _io_b_T_1 = tail(_io_b_T, 1) @[SubModule.scala 29:16]
    io.b <= _io_b_T_1 @[SubModule.scala 29:8]

  module SubModule :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip inp : UInt<4>, out : UInt<4>}

    inst submodule_inst of SubModuleSubMod @[SubModule.scala 18:30]
    submodule_inst.clock <= clock
    submodule_inst.reset <= reset

    wire _T : {flip inp : UInt<4>, out : UInt<4>}
    io <= _T
    submodule_inst.io.a <= _T.inp
    _T.out <= submodule_inst.io.b @[SubModule.scala 20:10]