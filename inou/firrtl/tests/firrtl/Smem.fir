;buildInfoPackage: chisel3, version: 3.4.2, scalaVersion: 2.12.12, sbtVersion: 1.3.10
circuit ReadWriteSmem : 
  module ReadWriteSmem : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip enable : UInt<1>, flip write : UInt<1>, flip addr : UInt<10>, flip dataIn : UInt<32>, dataOut : UInt<32>}
    
    smem mem : UInt<32>[1024], undefined @[ReadWriteSmem.scala 14:24]
    write mport MPORT = mem[io.addr], clock
    MPORT <= io.dataIn
    wire _io_dataOut_WIRE : UInt @[ReadWriteSmem.scala 17:25]
    _io_dataOut_WIRE is invalid @[ReadWriteSmem.scala 17:25]
    when io.enable : @[ReadWriteSmem.scala 17:25]
      _io_dataOut_WIRE <= io.addr @[ReadWriteSmem.scala 17:25]
      node _io_dataOut_T = or(_io_dataOut_WIRE, UInt<10>("h00")) @[ReadWriteSmem.scala 17:25]
      node _io_dataOut_T_1 = bits(_io_dataOut_T, 9, 0) @[ReadWriteSmem.scala 17:25]
      read mport io_dataOut_MPORT = mem[_io_dataOut_T_1], clock @[ReadWriteSmem.scala 17:25]
      skip @[ReadWriteSmem.scala 17:25]
    io.dataOut <= io_dataOut_MPORT @[ReadWriteSmem.scala 17:14]
    
