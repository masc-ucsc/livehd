circuit Top :
  module Sub :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip a : UInt<4>, b : UInt<4>}

    node _T   = add(io.a, UInt<1>("h1")) @[SubModule.scala 29:16]
    node _T_1 = tail(_T, 1) @[SubModule.scala 29:16]
    io.b <= _T_1 @[SubModule.scala 29:8]

  module Top :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip inp : UInt<4>, out : UInt<4>}

    inst sub of Sub @[SubModule.scala 18:30]
    sub.clock <= clock
    sub.reset <= reset

    wire _T : {flip inp : UInt<4>, out : UInt<4>}
    io <= _T
    sub.io.a <= _T.inp
    _T.out <= sub.io.b @[SubModule.scala 20:10]