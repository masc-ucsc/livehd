circuit MaskedReadWriteSmem :
  module MaskedReadWriteSmem :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip enable : UInt<1>, flip write : UInt<1>, flip addr : UInt<10>, flip mask : UInt<1>[4], flip dataIn : UInt<8>[4], dataOut : UInt<8>[4]}

    mem mem : @[MaskedReadWriteSmem.scala 16:24]
      data-type => UInt<8>[4]
      depth => 1024
      read-latency => 1
      write-latency => 1
      reader => MPORT_1
      writer => MPORT
      read-under-write => undefined
    mem.MPORT_1.addr is invalid @[MaskedReadWriteSmem.scala 16:24]
    mem.MPORT_1.clk is invalid @[MaskedReadWriteSmem.scala 16:24]
    mem.MPORT_1.en <= UInt<1>("h0") @[MaskedReadWriteSmem.scala 16:24]
    mem.MPORT.addr is invalid @[MaskedReadWriteSmem.scala 16:24]
    mem.MPORT.clk is invalid @[MaskedReadWriteSmem.scala 16:24]
    mem.MPORT.en <= UInt<1>("h0") @[MaskedReadWriteSmem.scala 16:24]
    mem.MPORT.data is invalid @[MaskedReadWriteSmem.scala 16:24]
    mem.MPORT.mask is invalid @[MaskedReadWriteSmem.scala 16:24]
    mem.MPORT.addr <= io.addr
    mem.MPORT.clk <= clock
    mem.MPORT.en <= UInt<1>("h1")
    mem.MPORT.mask[0] <= UInt<1>("h0")
    mem.MPORT.mask[1] <= UInt<1>("h0")
    mem.MPORT.mask[2] <= UInt<1>("h0")
    mem.MPORT.mask[3] <= UInt<1>("h0")
    when io.mask[0] :
      mem.MPORT.data[0] <= io.dataIn[0]
      mem.MPORT.mask[0] <= UInt<1>("h1")
    when io.mask[1] :
      mem.MPORT.data[1] <= io.dataIn[1]
      mem.MPORT.mask[1] <= UInt<1>("h1")
    when io.mask[2] :
      mem.MPORT.data[2] <= io.dataIn[2]
      mem.MPORT.mask[2] <= UInt<1>("h1")
    when io.mask[3] :
      mem.MPORT.data[3] <= io.dataIn[3]
      mem.MPORT.mask[3] <= UInt<1>("h1")
    wire _WIRE : UInt<10> @[MaskedReadWriteSmem.scala 19:25]
    _WIRE is invalid @[MaskedReadWriteSmem.scala 19:25]
    when io.enable : @[MaskedReadWriteSmem.scala 19:25]
      _WIRE <= io.addr @[MaskedReadWriteSmem.scala 19:25]
      node _T = or(_WIRE, UInt<10>("h0")) @[MaskedReadWriteSmem.scala 19:25]
      node _T_1 = bits(_T, 9, 0) @[MaskedReadWriteSmem.scala 19:25]
      mem.MPORT_1.en <= UInt<1>("h1") @[MaskedReadWriteSmem.scala 19:25]
      mem.MPORT_1.addr <= _T_1 @[MaskedReadWriteSmem.scala 19:25]
      mem.MPORT_1.clk <= clock @[MaskedReadWriteSmem.scala 19:25]
    io.dataOut[0] <= mem.MPORT_1.data[0] @[MaskedReadWriteSmem.scala 19:14]
    io.dataOut[1] <= mem.MPORT_1.data[1] @[MaskedReadWriteSmem.scala 19:14]
    io.dataOut[2] <= mem.MPORT_1.data[2] @[MaskedReadWriteSmem.scala 19:14]
    io.dataOut[3] <= mem.MPORT_1.data[3] @[MaskedReadWriteSmem.scala 19:14]
