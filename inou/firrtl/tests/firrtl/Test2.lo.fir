circuit Test2 :
  module Test2 :
    input clock : Clock
    input reset : UInt<1>
    input io_in_val : UInt<5>
    output io_out_head : UInt<4>
    output io_out_extractS : UInt<3>
    output io_out_extractI : UInt<3>
    output io_out_extractE : UInt<3>

    node _io_out_head_T = head(io_in_val, 4) @[Test2.scala 38:32]
    node _io_out_extractS_T = bits(io_in_val, 3, 1) @[Test2.scala 41:31]
    node _eI_T = bits(io_in_val, 3, 1) @[Test2.scala 44:18]
    node _eE_T = bits(io_in_val, 3, 1) @[Test2.scala 48:18]
    node eI = _eI_T @[Test2.scala 43:16 Test2.scala 44:6]
    node eE = _eE_T @[Test2.scala 47:16 Test2.scala 48:6]
    io_out_head <= _io_out_head_T @[Test2.scala 38:15]
    io_out_extractS <= _io_out_extractS_T @[Test2.scala 41:19]
    io_out_extractI <= eI @[Test2.scala 45:19]
    io_out_extractE <= eE @[Test2.scala 49:19]