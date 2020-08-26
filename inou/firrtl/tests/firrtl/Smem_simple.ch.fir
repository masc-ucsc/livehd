;buildInfoPackage: chisel3, version: 3.3.1, scalaVersion: 2.12.11, sbtVersion: 1.3.10
circuit Smem_simple : 
  module Smem_simple : 
    input clock : Clock
    output io : {flip enable : UInt<1>, flip addr : UInt<10>, dataOut : UInt<32>}
    
    smem mem : UInt<32>[1024], undefined @[Smem.scala 15:24]
    when io.enable : @[Smem.scala 18:25]
      read mport rdport = mem[io.addr], clock @[Smem.scala 18:25]
    io.dataOut <= rdport @[Smem.scala 18:14]
    
