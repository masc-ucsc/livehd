circuit Smem :
  module Smem :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip addr : UInt<10>, dataOut : UInt<32>}

    smem mem : UInt<32> [1024] @[Smem.scala 15:24]
    node _T = gt(io.addr, UInt<8>("hc8")) @[Smem.scala 18:43]
    wire _T_1 : UInt @[Smem.scala 18:25]
    _T_1 is invalid @[Smem.scala 18:25]
    when _T : @[Smem.scala 18:25]
      _T_1 <= io.addr @[Smem.scala 18:25]
      node _T_2 = or(_T_1, UInt<10>("h0")) @[Smem.scala 18:25]
      node _T_3 = bits(_T_2, 9, 0) @[Smem.scala 18:25]
      read mport _T_4 = mem[_T_3], clock @[Smem.scala 18:25]
      skip
    io.dataOut <= _T_4 @[Smem.scala 18:14]