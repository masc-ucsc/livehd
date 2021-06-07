;buildInfoPackage: chisel3, version: 3.4.2, scalaVersion: 2.12.12, sbtVersion: 1.3.10
circuit MaskedSmem : 
  module MaskedSmem : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip enable : UInt<1>, flip write : UInt<1>, flip addr : UInt<10>, flip mask : UInt<1>[4], flip dataIn : UInt<8>[4], dataOut : UInt<8>[4]}
    
    smem mem : UInt<8>[4][1024], undefined @[MaskedSmem.scala 16:24]
    write mport MPORT = mem[io.addr], clock
    when io.mask[0] :
      MPORT[0] <= io.dataIn[0]
      skip
    when io.mask[1] :
      MPORT[1] <= io.dataIn[1]
      skip
    when io.mask[2] :
      MPORT[2] <= io.dataIn[2]
      skip
    when io.mask[3] :
      MPORT[3] <= io.dataIn[3]
      skip
    wire _WIRE : UInt @[MaskedSmem.scala 19:25]
    _WIRE is invalid @[MaskedSmem.scala 19:25]
    when io.enable : @[MaskedSmem.scala 19:25]
      _WIRE <= io.addr @[MaskedSmem.scala 19:25]
      node _T = or(_WIRE, UInt<10>("h00")) @[MaskedSmem.scala 19:25]
      node _T_1 = bits(_T, 9, 0) @[MaskedSmem.scala 19:25]
      read mport MPORT_1 = mem[_T_1], clock @[MaskedSmem.scala 19:25]
      skip @[MaskedSmem.scala 19:25]
    io.dataOut[0] <= MPORT_1[0] @[MaskedSmem.scala 19:14]
    io.dataOut[1] <= MPORT_1[1] @[MaskedSmem.scala 19:14]
    io.dataOut[2] <= MPORT_1[2] @[MaskedSmem.scala 19:14]
    io.dataOut[3] <= MPORT_1[3] @[MaskedSmem.scala 19:14]
    
