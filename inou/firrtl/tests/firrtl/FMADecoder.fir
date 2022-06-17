circuit FMADecoder :
  module FMADecoder : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip uopc : UInt<7>, cmd : UInt<2>}
    
    node _T = and(io.uopc, UInt<7>("h027")) @[Decode.scala 14:65]
    node _T_1 = eq(_T, UInt<7>("h00")) @[Decode.scala 14:121]
    node _T_2 = and(io.uopc, UInt<7>("h012")) @[Decode.scala 14:65]
    node _T_3 = eq(_T_2, UInt<7>("h02")) @[Decode.scala 14:121]
    node _T_4 = and(io.uopc, UInt<7>("h0b")) @[Decode.scala 14:65]
    node _T_5 = eq(_T_4, UInt<7>("h0b")) @[Decode.scala 14:121]
    node _T_6 = and(io.uopc, UInt<7>("h0e")) @[Decode.scala 14:65]
    node _T_7 = eq(_T_6, UInt<7>("h0e")) @[Decode.scala 14:121]
    node _T_8 = or(UInt<1>("h00"), _T_1) @[Decode.scala 15:30]
    node _T_9 = or(_T_8, _T_3) @[Decode.scala 15:30]
    node _T_10 = or(_T_9, _T_5) @[Decode.scala 15:30]
    node _T_11 = or(_T_10, _T_7) @[Decode.scala 15:30]
    node _T_12 = and(io.uopc, UInt<7>("h013")) @[Decode.scala 14:65]
    node _T_13 = eq(_T_12, UInt<7>("h00")) @[Decode.scala 14:121]
    node _T_14 = and(io.uopc, UInt<7>("h013")) @[Decode.scala 14:65]
    node _T_15 = eq(_T_14, UInt<7>("h03")) @[Decode.scala 14:121]
    node _T_16 = and(io.uopc, UInt<7>("h0f")) @[Decode.scala 14:65]
    node _T_17 = eq(_T_16, UInt<7>("h0f")) @[Decode.scala 14:121]
    node _T_18 = or(UInt<1>("h00"), _T_13) @[Decode.scala 15:30]
    node _T_19 = or(_T_18, _T_15) @[Decode.scala 15:30]
    node _T_20 = or(_T_19, _T_17) @[Decode.scala 15:30]
    node cmd = cat(_T_20, _T_11) @[Cat.scala 29:58]
    io.cmd <= cmd @[fpu.scala 152:10]
    
