circuit MaxN :
  module MaxN :
    input clock : Clock
    input reset : UInt<1>
    input io_ins_0 : UInt<16>
    input io_ins_1 : UInt<16>
    input io_ins_2 : UInt<16>
    input io_ins_3 : UInt<16>
    input io_ins_4 : UInt<16>
    input io_ins_5 : UInt<16>
    input io_ins_6 : UInt<16>
    input io_ins_7 : UInt<16>
    output io_out : UInt<16>

    node _T = gt(io_ins_0, io_ins_1) @[MaxN.scala 13:46]
    node _T_1 = mux(_T, io_ins_0, io_ins_1) @[MaxN.scala 13:43]
    node _T_2 = gt(_T_1, io_ins_2) @[MaxN.scala 13:46]
    node _T_3 = mux(_T_2, _T_1, io_ins_2) @[MaxN.scala 13:43]
    node _T_4 = gt(_T_3, io_ins_3) @[MaxN.scala 13:46]
    node _T_5 = mux(_T_4, _T_3, io_ins_3) @[MaxN.scala 13:43]
    node _T_6 = gt(_T_5, io_ins_4) @[MaxN.scala 13:46]
    node _T_7 = mux(_T_6, _T_5, io_ins_4) @[MaxN.scala 13:43]
    node _T_8 = gt(_T_7, io_ins_5) @[MaxN.scala 13:46]
    node _T_9 = mux(_T_8, _T_7, io_ins_5) @[MaxN.scala 13:43]
    node _T_10 = gt(_T_9, io_ins_6) @[MaxN.scala 13:46]
    node _T_11 = mux(_T_10, _T_9, io_ins_6) @[MaxN.scala 13:43]
    node _T_12 = gt(_T_11, io_ins_7) @[MaxN.scala 13:46]
    node _T_13 = mux(_T_12, _T_11, io_ins_7) @[MaxN.scala 13:43]
    io_out <= _T_13 @[MaxN.scala 19:10]