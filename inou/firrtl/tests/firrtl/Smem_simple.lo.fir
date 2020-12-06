circuit Smem_simple :
  module Smem_simple :
    input clock : Clock
    input io_enable : UInt<1>
    input io_addr : UInt<10>
    output io_dataOut : UInt<32>

    mem mem : @[Smem.scala 15:24]
      data-type => UInt<32>
      depth => 1024
      read-latency => 1
      write-latency => 1
      reader => rdport
      read-under-write => undefined
    node _GEN_0 = validif(io_enable, io_addr) @[Smem.scala 18:25 Smem.scala 18:25]
    node _GEN_1 = validif(io_enable, clock) @[Smem.scala 18:25 Smem.scala 18:25]
    node _GEN_2 = mux(io_enable, UInt<1>("h1"), UInt<1>("h0")) @[Smem.scala 18:25 Smem.scala 18:25 Smem.scala 15:24]
    io_dataOut <= mem.rdport.data @[Smem.scala 18:14]
    mem.rdport.addr <= _GEN_0
    mem.rdport.en <= _GEN_2
    mem.rdport.clk <= _GEN_1