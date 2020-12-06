circuit Smem :
  module Smem :
    input clock : Clock
    input reset : UInt<1>
    input io_addr : UInt<10>
    output io_dataOut : UInt<32>

    mem mem : @[Smem.scala 15:24]
      data-type => UInt<32>
      depth => 1024
      read-latency => 1
      write-latency => 1
      reader => _T_4
      read-under-write => undefined
    node _T = gt(io_addr, UInt<8>("hc8")) @[Smem.scala 18:43]
    node _GEN_0 = validif(_T, io_addr) @[Smem.scala 18:25 Smem.scala 18:25]
    node _T_1 = _GEN_0 @[Smem.scala 18:25]
    node _T_2 = or(_T_1, UInt<10>("h0")) @[Smem.scala 18:25]
    node _T_3 = bits(_T_2, 9, 0) @[Smem.scala 18:25]
    node _GEN_1 = mux(_T, UInt<1>("h1"), UInt<1>("h0")) @[Smem.scala 18:25 Smem.scala 18:25 Smem.scala 15:24]
    node _GEN_2 = validif(_T, _T_3) @[Smem.scala 18:25 Smem.scala 18:25]
    node _GEN_3 = validif(_T, clock) @[Smem.scala 18:25 Smem.scala 18:25]
    io_dataOut <= mem._T_4.data @[Smem.scala 18:14]
    mem._T_4.addr <= _GEN_2
    mem._T_4.en <= _GEN_1
    mem._T_4.clk <= _GEN_3