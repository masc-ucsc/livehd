circuit TrivialArith :
  module TrivialArith :
    input clock : Clock
    input reset : UInt<1>
    input io_value1 : UInt<8>
    input io_value2 : UInt<8>
    input io_value3 : UInt<8>
    output io_outputAdd : UInt<10>
    output io_outputMul : UInt<16>

    node _T = add(io_value1, io_value2) @[TrivialArith.scala 16:29]
    node _T_1 = add(_T, io_value3) @[TrivialArith.scala 16:42]
    node _T_2 = mul(io_value1, io_value2) @[TrivialArith.scala 17:29]
    io_outputAdd <= _T_1 @[TrivialArith.scala 16:16]
    io_outputMul <= _T_2 @[TrivialArith.scala 17:16]