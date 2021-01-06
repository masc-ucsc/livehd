circuit Test2 :
  module Test2 :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip in_val : UInt<5>, out_head : UInt<4>, out_extractS : UInt<3>, out_extractI : UInt<3>, out_extractE : UInt<3>}

    node _io_out_head_T = head(io.in_val, 4) @[Test2.scala 38:32]
    io.out_head <= _io_out_head_T @[Test2.scala 38:15]
    node _io_out_extractS_T = bits(io.in_val, 3, 1) @[Test2.scala 41:31]
    io.out_extractS <= _io_out_extractS_T @[Test2.scala 41:19]
    wire eI : UInt<3> @[Test2.scala 43:16]
    node _eI_T = bits(io.in_val, 3, 1) @[Test2.scala 44:18]
    eI <= _eI_T @[Test2.scala 44:6]
    io.out_extractI <= eI @[Test2.scala 45:19]
    wire eE : UInt<3> @[Test2.scala 47:16]
    node _eE_T = bits(io.in_val, 3, 1) @[Test2.scala 48:18]
    eE <= _eE_T @[Test2.scala 48:6]
    io.out_extractE <= eE @[Test2.scala 49:19]