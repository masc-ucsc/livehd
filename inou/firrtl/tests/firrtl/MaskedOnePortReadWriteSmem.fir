;buildInfoPackage: chisel3, version: 3.4.2, scalaVersion: 2.12.12, sbtVersion: 1.3.10
circuit MaskedOnePortReadWriteSmem : 
  module MaskedOnePortReadWriteSmem : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip enable : UInt<1>, flip write : UInt<1>, flip mask : UInt<1>[2], flip addr : UInt<10>, flip dataIn : UInt<32>[2], dataOut : UInt<32>[2]}
    
    smem mem : UInt<32>[2][1024], undefined @[MaskedOnePortReadWriteSmem.scala 14:24]
    io.dataOut[0] is invalid @[MaskedOnePortReadWriteSmem.scala 15:14]
    io.dataOut[1] is invalid @[MaskedOnePortReadWriteSmem.scala 15:14]
    when io.enable : @[MaskedOnePortReadWriteSmem.scala 16:19]
      infer mport rdwrPort = mem[io.addr], clock @[MaskedOnePortReadWriteSmem.scala 17:23]
      when io.write : @[MaskedOnePortReadWriteSmem.scala 18:21]
        when io.mask[0] : @[MaskedOnePortReadWriteSmem.scala 19:24]
          rdwrPort[0] <= io.dataIn[0] @[MaskedOnePortReadWriteSmem.scala 20:21]
          skip @[MaskedOnePortReadWriteSmem.scala 19:24]
        when io.mask[1] : @[MaskedOnePortReadWriteSmem.scala 22:24]
          rdwrPort[1] <= io.dataIn[1] @[MaskedOnePortReadWriteSmem.scala 23:21]
          skip @[MaskedOnePortReadWriteSmem.scala 22:24]
        skip @[MaskedOnePortReadWriteSmem.scala 18:21]
      else : @[MaskedOnePortReadWriteSmem.scala 25:17]
        io.dataOut[0] <= rdwrPort[0] @[MaskedOnePortReadWriteSmem.scala 25:30]
        io.dataOut[1] <= rdwrPort[1] @[MaskedOnePortReadWriteSmem.scala 25:30]
        skip @[MaskedOnePortReadWriteSmem.scala 25:17]
      skip @[MaskedOnePortReadWriteSmem.scala 16:19]
    
