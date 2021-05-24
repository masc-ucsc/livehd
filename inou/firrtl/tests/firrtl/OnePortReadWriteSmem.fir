;buildInfoPackage: chisel3, version: 3.4.2, scalaVersion: 2.12.12, sbtVersion: 1.3.10
circuit OnePortReadWriteSmem : 
  module OnePortReadWriteSmem : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip enable : UInt<1>, flip write : UInt<1>, flip addr : UInt<10>, flip dataIn : UInt<32>, dataOut : UInt<32>}
    
    smem mem : UInt<32>[1024], undefined @[OnePortReadWriteSmem.scala 14:24]
    io.dataOut is invalid @[OnePortReadWriteSmem.scala 15:14]
    when io.enable : @[OnePortReadWriteSmem.scala 16:19]
      infer mport rdwrPort = mem[io.addr], clock @[OnePortReadWriteSmem.scala 17:23]
      when io.write : @[OnePortReadWriteSmem.scala 18:21]
        rdwrPort <= io.dataIn @[OnePortReadWriteSmem.scala 18:32]
        skip @[OnePortReadWriteSmem.scala 18:21]
      else : @[OnePortReadWriteSmem.scala 19:21]
        io.dataOut <= rdwrPort @[OnePortReadWriteSmem.scala 19:34]
        skip @[OnePortReadWriteSmem.scala 19:21]
      skip @[OnePortReadWriteSmem.scala 16:19]
    
