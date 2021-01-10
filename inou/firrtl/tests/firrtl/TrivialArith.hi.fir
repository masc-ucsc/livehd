circuit TrivialArith :
  module TrivialArith :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip value1 : UInt<8>, flip value2 : UInt<8>, flip value3 : UInt<8>, outputAdd : UInt<10>, outputMul : UInt<16>}

    node _io_outputAdd_T = add(io.value1, io.value2) @[TrivialArith.scala 16:29]
    node _io_outputAdd_T_1 = add(_io_outputAdd_T, io.value3) @[TrivialArith.scala 16:42]
    io.outputAdd <= _io_outputAdd_T_1 @[TrivialArith.scala 16:16]
    node _io_outputMul_T = mul(io.value1, io.value2) @[TrivialArith.scala 17:29]
    io.outputMul <= _io_outputMul_T @[TrivialArith.scala 17:16]