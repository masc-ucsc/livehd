circuit TrivialArith :
  module TrivialArith :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip value1 : UInt<8>, flip value2 : UInt<8>, flip value3 : UInt<8>, outputAdd : UInt<10>, outputMul : UInt<16>}

    node _T = add(io.value1, io.value2) @[TrivialArith.scala 16:29]
    node _T_1 = add(_T, io.value3) @[TrivialArith.scala 16:42]
    io.outputAdd <= _T_1 @[TrivialArith.scala 16:16]
    node _T_2 = mul(io.value1, io.value2) @[TrivialArith.scala 17:29]
    io.outputMul <= _T_2 @[TrivialArith.scala 17:16]