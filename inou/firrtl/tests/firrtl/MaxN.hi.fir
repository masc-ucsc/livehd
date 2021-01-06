circuit MaxN :
  module MaxN :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip ins : UInt<8>[3], out : UInt<8>}

    node _io_out_T = gt(io.ins[0], io.ins[1]) @[MaxN.scala 13:46]
    node _io_out_T_1 = mux(_io_out_T, io.ins[0], io.ins[1]) @[MaxN.scala 13:43]
    node _io_out_T_2 = gt(_io_out_T_1, io.ins[2]) @[MaxN.scala 13:46]
    node _io_out_T_3 = mux(_io_out_T_2, _io_out_T_1, io.ins[2]) @[MaxN.scala 13:43]
    io.out <= _io_out_T_3 @[MaxN.scala 19:10]