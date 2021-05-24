circuit OnePortReadWriteSmem :
  module OnePortReadWriteSmem :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip enable : UInt<1>, flip write : UInt<1>, flip addr : UInt<10>, flip dataIn : UInt<32>, dataOut : UInt<32>}

    mem mem : @[OnePortReadWriteSmem.scala 14:24]
      data-type => UInt<32>
      depth => 1024
      read-latency => 1
      write-latency => 1
      readwriter => rdwrPort
      read-under-write => undefined
    mem.rdwrPort.addr is invalid @[OnePortReadWriteSmem.scala 14:24]
    mem.rdwrPort.clk is invalid @[OnePortReadWriteSmem.scala 14:24]
    mem.rdwrPort.wmode <= UInt<1>("h0") @[OnePortReadWriteSmem.scala 14:24]
    mem.rdwrPort.en <= UInt<1>("h0") @[OnePortReadWriteSmem.scala 14:24]
    mem.rdwrPort.wdata is invalid @[OnePortReadWriteSmem.scala 14:24]
    mem.rdwrPort.wmask is invalid @[OnePortReadWriteSmem.scala 14:24]
    io.dataOut is invalid @[OnePortReadWriteSmem.scala 15:14]
    when io.enable : @[OnePortReadWriteSmem.scala 16:19]
      mem.rdwrPort.addr <= io.addr @[OnePortReadWriteSmem.scala 17:23]
      mem.rdwrPort.clk <= clock @[OnePortReadWriteSmem.scala 17:23]
      mem.rdwrPort.en <= UInt<1>("h1") @[OnePortReadWriteSmem.scala 17:23]
      mem.rdwrPort.wmask <= UInt<1>("h0") @[OnePortReadWriteSmem.scala 17:23]
      when io.write : @[OnePortReadWriteSmem.scala 18:21]
        mem.rdwrPort.wdata <= io.dataIn @[OnePortReadWriteSmem.scala 18:32]
        mem.rdwrPort.wmask <= UInt<1>("h1") @[OnePortReadWriteSmem.scala 18:32]
        mem.rdwrPort.wmode <= UInt<1>("h1") @[OnePortReadWriteSmem.scala 18:32]
      else :
        io.dataOut <= mem.rdwrPort.rdata @[OnePortReadWriteSmem.scala 19:34]
