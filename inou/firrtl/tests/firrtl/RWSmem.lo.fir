circuit RWSmem :
  module RWSmem :
    input clock : Clock
    input reset : UInt<1>
    input io_enable : UInt<1>
    input io_write : UInt<1>
    input io_addr : UInt<10>
    input io_dataIn : UInt<32>
    output io_dataOut : UInt<32>

    mem mem : @[RWSmem.scala 15:24]
      data-type => UInt<32>
      depth => 1024
      read-latency => 1
      write-latency => 1
      readwriter => rdwrPort
      read-under-write => undefined
    node _GEN_0 = validif(io_write, io_dataIn) @[RWSmem.scala 19:21 RWSmem.scala 19:32]
    node _GEN_1 = mux(io_write, UInt<1>("h1"), UInt<1>("h0")) @[RWSmem.scala 19:21 RWSmem.scala 19:32 RWSmem.scala 18:23]
    node _GEN_2 = validif(eq(io_write, UInt<1>("h0")), mem.rdwrPort.rdata) @[RWSmem.scala 19:21 RWSmem.scala 20:34]
    node _GEN_3 = validif(io_enable, io_addr) @[RWSmem.scala 17:19 RWSmem.scala 18:23]
    node _GEN_4 = validif(io_enable, clock) @[RWSmem.scala 17:19 RWSmem.scala 18:23]
    node _GEN_5 = mux(io_enable, UInt<1>("h1"), UInt<1>("h0")) @[RWSmem.scala 17:19 RWSmem.scala 18:23 RWSmem.scala 15:24]
    node _GEN_6 = validif(io_enable, _GEN_1) @[RWSmem.scala 17:19]
    node _GEN_7 = validif(io_enable, _GEN_0) @[RWSmem.scala 17:19]
    node _GEN_8 = mux(io_enable, _GEN_1, UInt<1>("h0")) @[RWSmem.scala 17:19 RWSmem.scala 15:24]
    node _GEN_9 = validif(io_enable, _GEN_2) @[RWSmem.scala 17:19]
    io_dataOut <= _GEN_9
    mem.rdwrPort.addr <= _GEN_3
    mem.rdwrPort.en <= _GEN_5
    mem.rdwrPort.clk <= _GEN_4
    mem.rdwrPort.wmode <= _GEN_8
    mem.rdwrPort.wdata <= _GEN_7
    mem.rdwrPort.wmask <= _GEN_6