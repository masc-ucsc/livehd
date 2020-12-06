circuit TrivialAdd :
  module TrivialAdd :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip value1 : UInt<8>, flip value2 : UInt<8>, flip value3 : UInt<8>, outputAdd : UInt<10>, outputSub : UInt<9>}

    node _T = add(io.value1, io.value2)
    node _T_1 = add(_T, io.value3)
    io.outputAdd <= _T_1
    node _T_2 = sub(io.value1, io.value2)
    io.outputSub <= _T_2