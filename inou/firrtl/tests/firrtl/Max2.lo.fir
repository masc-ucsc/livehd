circuit Max2 :
  module Max2 :
    input clock : Clock
    input reset : UInt<1>
    input io_in0 : UInt<8>
    input io_in1 : UInt<8>
    output io_out : UInt<8>

    node _T = gt(io_in0, io_in1) @[Max2.scala 17:24]
    node _T_1 = mux(_T, io_in0, io_in1) @[Max2.scala 17:16]
    io_out <= _T_1 @[Max2.scala 17:10]