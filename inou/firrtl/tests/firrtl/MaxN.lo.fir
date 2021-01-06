circuit MaxN :
  module MaxN :
    input clock : Clock
    input reset : UInt<1>
    input io_ins_0 : UInt<8>
    input io_ins_1 : UInt<8>
    input io_ins_2 : UInt<8>
    output io_out : UInt<8>

    node _io_out_T = gt(io_ins_0, io_ins_1) @[MaxN.scala 13:46]
    node _io_out_T_1 = mux(_io_out_T, io_ins_0, io_ins_1) @[MaxN.scala 13:43]
    node _io_out_T_2 = gt(_io_out_T_1, io_ins_2) @[MaxN.scala 13:46]
    node _io_out_T_3 = mux(_io_out_T_2, _io_out_T_1, io_ins_2) @[MaxN.scala 13:43]
    io_out <= _io_out_T_3 @[MaxN.scala 19:10]