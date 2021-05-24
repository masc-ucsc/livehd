circuit ReadWriteSmem :
  module ReadWriteSmem :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip enable : UInt<1>, flip write : UInt<1>, flip addr : UInt<10>, flip dataIn : UInt<32>, dataOut : UInt<32>}

    mem mem : @[ReadWriteSmem.scala 14:24]
      data-type => UInt<32>
      depth => 1024
      read-latency => 1
      write-latency => 1
      reader => io_dataOut_MPORT
      writer => MPORT
      read-under-write => undefined
    mem.io_dataOut_MPORT.addr is invalid @[ReadWriteSmem.scala 14:24]
    mem.io_dataOut_MPORT.clk is invalid @[ReadWriteSmem.scala 14:24]
    mem.io_dataOut_MPORT.en <= UInt<1>("h0") @[ReadWriteSmem.scala 14:24]
    mem.MPORT.addr is invalid @[ReadWriteSmem.scala 14:24]
    mem.MPORT.clk is invalid @[ReadWriteSmem.scala 14:24]
    mem.MPORT.en <= UInt<1>("h0") @[ReadWriteSmem.scala 14:24]
    mem.MPORT.data is invalid @[ReadWriteSmem.scala 14:24]
    mem.MPORT.mask is invalid @[ReadWriteSmem.scala 14:24]
    mem.MPORT.addr <= io.addr
    mem.MPORT.clk <= clock
    mem.MPORT.en <= UInt<1>("h1")
    mem.MPORT.mask <= UInt<1>("h0")
    mem.MPORT.data <= io.dataIn
    mem.MPORT.mask <= UInt<1>("h1")
    wire _io_dataOut_WIRE : UInt<10> @[ReadWriteSmem.scala 17:25]
    _io_dataOut_WIRE is invalid @[ReadWriteSmem.scala 17:25]
    when io.enable : @[ReadWriteSmem.scala 17:25]
      _io_dataOut_WIRE <= io.addr @[ReadWriteSmem.scala 17:25]
      node _io_dataOut_T = or(_io_dataOut_WIRE, UInt<10>("h0")) @[ReadWriteSmem.scala 17:25]
      node _io_dataOut_T_1 = bits(_io_dataOut_T, 9, 0) @[ReadWriteSmem.scala 17:25]
      mem.io_dataOut_MPORT.en <= UInt<1>("h1") @[ReadWriteSmem.scala 17:25]
      mem.io_dataOut_MPORT.addr <= _io_dataOut_T_1 @[ReadWriteSmem.scala 17:25]
      mem.io_dataOut_MPORT.clk <= clock @[ReadWriteSmem.scala 17:25]
    io.dataOut <= mem.io_dataOut_MPORT.data @[ReadWriteSmem.scala 17:14]
