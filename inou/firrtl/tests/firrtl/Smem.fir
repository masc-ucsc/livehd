;buildInfoPackage: chisel3, version: 3.4.2, scalaVersion: 2.12.12, sbtVersion: 1.3.10
circuit Smem : 
  module Smem : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip enable : UInt<1>, flip write : UInt<1>, flip addr : UInt<10>, flip dataIn : UInt<32>, dataOut : UInt<32>}
    
    smem mem : UInt<32>[1024], undefined @[Smem.scala 14:24]
    write mport MPORT = mem[io.addr], clock
    MPORT <= io.dataIn
    wire _io_dataOut_WIRE : UInt @[Smem.scala 17:25]
    _io_dataOut_WIRE is invalid @[Smem.scala 17:25]
    when io.enable : @[Smem.scala 17:25]
      _io_dataOut_WIRE <= io.addr @[Smem.scala 17:25]
      node _io_dataOut_T = or(_io_dataOut_WIRE, UInt<10>("h00")) @[Smem.scala 17:25]
      node _io_dataOut_T_1 = bits(_io_dataOut_T, 9, 0) @[Smem.scala 17:25]
      read mport io_dataOut_MPORT = mem[_io_dataOut_T_1], clock @[Smem.scala 17:25]
      skip @[Smem.scala 17:25]
    io.dataOut <= io_dataOut_MPORT @[Smem.scala 17:14]
    
