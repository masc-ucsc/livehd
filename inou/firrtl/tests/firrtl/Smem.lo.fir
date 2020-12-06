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
      reader => io_dataOut_MPORT
      read-under-write => undefined
    node _io_dataOut_T = gt(io_addr, UInt<8>("hc8")) @[Smem.scala 18:43]
    node _GEN_0 = validif(_io_dataOut_T, io_addr) @[Smem.scala 18:25 Smem.scala 18:25]
    node _io_dataOut_WIRE = _GEN_0 @[Smem.scala 18:25]
    node _io_dataOut_T_1 = or(_io_dataOut_WIRE, UInt<10>("h0")) @[Smem.scala 18:25]
    node _io_dataOut_T_2 = bits(_io_dataOut_T_1, 9, 0) @[Smem.scala 18:25]
    node _GEN_1 = mux(_io_dataOut_T, UInt<1>("h1"), UInt<1>("h0")) @[Smem.scala 18:25 Smem.scala 18:25 Smem.scala 15:24]
    node _GEN_2 = validif(_io_dataOut_T, _io_dataOut_T_2) @[Smem.scala 18:25 Smem.scala 18:25]
    node _GEN_3 = validif(_io_dataOut_T, clock) @[Smem.scala 18:25 Smem.scala 18:25]
    io_dataOut <= mem.io_dataOut_MPORT.data @[Smem.scala 18:14]
    mem.io_dataOut_MPORT.addr <= _GEN_2
    mem.io_dataOut_MPORT.en <= _GEN_1
    mem.io_dataOut_MPORT.clk <= _GEN_3