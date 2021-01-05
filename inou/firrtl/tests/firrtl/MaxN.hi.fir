circuit MaxN :
  module MaxN :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip ins : UInt<16>[8], out : UInt<16>}

    node _T = gt(io.ins[0], io.ins[1]) @[MaxN.scala 13:46]
    node _T_1 = mux(_T, io.ins[0], io.ins[1]) @[MaxN.scala 13:43]
    node _T_2 = gt(_T_1, io.ins[2]) @[MaxN.scala 13:46]
    node _T_3 = mux(_T_2, _T_1, io.ins[2]) @[MaxN.scala 13:43]
    node _T_4 = gt(_T_3, io.ins[3]) @[MaxN.scala 13:46]
    node _T_5 = mux(_T_4, _T_3, io.ins[3]) @[MaxN.scala 13:43]
    node _T_6 = gt(_T_5, io.ins[4]) @[MaxN.scala 13:46]
    node _T_7 = mux(_T_6, _T_5, io.ins[4]) @[MaxN.scala 13:43]
    node _T_8 = gt(_T_7, io.ins[5]) @[MaxN.scala 13:46]
    node _T_9 = mux(_T_8, _T_7, io.ins[5]) @[MaxN.scala 13:43]
    node _T_10 = gt(_T_9, io.ins[6]) @[MaxN.scala 13:46]
    node _T_11 = mux(_T_10, _T_9, io.ins[6]) @[MaxN.scala 13:43]
    node _T_12 = gt(_T_11, io.ins[7]) @[MaxN.scala 13:46]
    node _T_13 = mux(_T_12, _T_11, io.ins[7]) @[MaxN.scala 13:43]
    io.out <= _T_13 @[MaxN.scala 19:10]