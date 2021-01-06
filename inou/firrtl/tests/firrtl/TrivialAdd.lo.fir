circuit TrivialAdd :
  module TrivialAdd :
    input clock : Clock
    input reset : UInt<1>
    input io_value1 : UInt<8>
    input io_value2 : UInt<8>
    input io_value3 : UInt<8>
    output io_outputAdd : UInt<10>
    output io_outputSub : UInt<9>

    node _T   = add(io_value1, io_value2)
    node _T_1 = add(_T, io_value3)
    node _T_2 = sub(io_value1, io_value2)
    io_outputAdd <= _T_1
    io_outputSub <= _T_2