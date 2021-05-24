circuit MaskedOnePortReadWriteSmem :
  module MaskedOnePortReadWriteSmem :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip enable : UInt<1>, flip write : UInt<1>, flip mask : UInt<1>[2], flip addr : UInt<10>, flip dataIn : UInt<32>[2], dataOut : UInt<32>[2]}

    mem mem : @[MaskedOnePortReadWriteSmem.scala 14:24]
      data-type => UInt<32>[2]
      depth => 1024
      read-latency => 1
      write-latency => 1
      readwriter => rdwrPort
      read-under-write => undefined
    mem.rdwrPort.addr is invalid @[MaskedOnePortReadWriteSmem.scala 14:24]
    mem.rdwrPort.clk is invalid @[MaskedOnePortReadWriteSmem.scala 14:24]
    mem.rdwrPort.wmode <= UInt<1>("h0") @[MaskedOnePortReadWriteSmem.scala 14:24]
    mem.rdwrPort.en <= UInt<1>("h0") @[MaskedOnePortReadWriteSmem.scala 14:24]
    mem.rdwrPort.wdata is invalid @[MaskedOnePortReadWriteSmem.scala 14:24]
    mem.rdwrPort.wmask is invalid @[MaskedOnePortReadWriteSmem.scala 14:24]
    io.dataOut[0] is invalid @[MaskedOnePortReadWriteSmem.scala 15:14]
    io.dataOut[1] is invalid @[MaskedOnePortReadWriteSmem.scala 15:14]
    when io.enable : @[MaskedOnePortReadWriteSmem.scala 16:19]
      mem.rdwrPort.addr <= io.addr @[MaskedOnePortReadWriteSmem.scala 17:23]
      mem.rdwrPort.clk <= clock @[MaskedOnePortReadWriteSmem.scala 17:23]
      mem.rdwrPort.en <= UInt<1>("h1") @[MaskedOnePortReadWriteSmem.scala 17:23]
      mem.rdwrPort.wmask[0] <= UInt<1>("h0") @[MaskedOnePortReadWriteSmem.scala 17:23]
      mem.rdwrPort.wmask[1] <= UInt<1>("h0") @[MaskedOnePortReadWriteSmem.scala 17:23]
      when io.write : @[MaskedOnePortReadWriteSmem.scala 18:21]
        when io.mask[0] : @[MaskedOnePortReadWriteSmem.scala 19:24]
          mem.rdwrPort.wdata[0] <= io.dataIn[0] @[MaskedOnePortReadWriteSmem.scala 20:21]
          mem.rdwrPort.wmask[0] <= UInt<1>("h1") @[MaskedOnePortReadWriteSmem.scala 20:21]
          mem.rdwrPort.wmode <= UInt<1>("h1") @[MaskedOnePortReadWriteSmem.scala 20:21]
        when io.mask[1] : @[MaskedOnePortReadWriteSmem.scala 22:24]
          mem.rdwrPort.wdata[1] <= io.dataIn[1] @[MaskedOnePortReadWriteSmem.scala 23:21]
          mem.rdwrPort.wmask[1] <= UInt<1>("h1") @[MaskedOnePortReadWriteSmem.scala 23:21]
          mem.rdwrPort.wmode <= UInt<1>("h1") @[MaskedOnePortReadWriteSmem.scala 23:21]
      else :
        io.dataOut[0] <= mem.rdwrPort.rdata[0] @[MaskedOnePortReadWriteSmem.scala 25:30]
        io.dataOut[1] <= mem.rdwrPort.rdata[1] @[MaskedOnePortReadWriteSmem.scala 25:30]
