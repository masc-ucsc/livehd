;buildInfoPackage: chisel3, version: 3.4.2, scalaVersion: 2.12.12, sbtVersion: 1.3.10
circuit Memo : 
  module Memo : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip wen : UInt<1>, flip wrAddr : UInt<8>, flip wrData : UInt<8>, flip ren : UInt<1>, flip rdAddr : UInt<8>, rdData : UInt<8>}
    
    cmem mem : UInt<8>[256] @[Memo.scala 23:16]
    when io.wen : @[Memo.scala 26:17]
      infer mport MPORT = mem[io.wrAddr], clock @[Memo.scala 26:22]
      MPORT <= io.wrData @[Memo.scala 26:34]
      skip @[Memo.scala 26:17]
    io.rdData <= UInt<1>("h00") @[Memo.scala 29:13]
    when io.ren : @[Memo.scala 30:17]
      infer mport io_rdData_MPORT = mem[io.rdAddr], clock @[Memo.scala 30:35]
      io.rdData <= io_rdData_MPORT @[Memo.scala 30:29]
      skip @[Memo.scala 30:17]
    
