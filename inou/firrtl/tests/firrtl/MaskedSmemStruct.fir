;buildInfoPackage: chisel3, version: 3.4.2, scalaVersion: 2.12.12, sbtVersion: 1.3.10
circuit MaskedSmemStruct : 
  module MaskedSmemStruct : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip enable : UInt<1>, flip write : UInt<1>, flip addr : UInt<10>, flip mask : UInt<1>[4], flip dataIn : {xx : {d1 : UInt<2>, d2 : UInt<2>}, yy : UInt<4>}[4], dataOut : {xx : {d1 : UInt<2>, d2 : UInt<2>}, yy : UInt<4>}[4]}
    
    smem my_mem : {xx : {d1 : UInt<2>, d2 : UInt<2>}, yy : UInt<4>}[4][1024], undefined @[MaskedSmemStruct.scala 30:27]
    write mport MPORT = my_mem[io.addr], clock
    when io.mask[0] :
      MPORT[0].yy <= io.dataIn[0].yy
      MPORT[0].xx.d2 <= io.dataIn[0].xx.d2
      MPORT[0].xx.d1 <= io.dataIn[0].xx.d1
      skip
    when io.mask[1] :
      MPORT[1].yy <= io.dataIn[1].yy
      MPORT[1].xx.d2 <= io.dataIn[1].xx.d2
      MPORT[1].xx.d1 <= io.dataIn[1].xx.d1
      skip
    when io.mask[2] :
      MPORT[2].yy <= io.dataIn[2].yy
      MPORT[2].xx.d2 <= io.dataIn[2].xx.d2
      MPORT[2].xx.d1 <= io.dataIn[2].xx.d1
      skip
    when io.mask[3] :
      MPORT[3].yy <= io.dataIn[3].yy
      MPORT[3].xx.d2 <= io.dataIn[3].xx.d2
      MPORT[3].xx.d1 <= io.dataIn[3].xx.d1
      skip
    wire rd_enable : UInt<1> @[MaskedSmemStruct.scala 35:23]
    node _T = gt(io.addr, UInt<6>("h020")) @[MaskedSmemStruct.scala 36:17]
    when _T : @[MaskedSmemStruct.scala 36:25]
      rd_enable <= io.enable @[MaskedSmemStruct.scala 37:15]
      skip @[MaskedSmemStruct.scala 36:25]
    else : @[MaskedSmemStruct.scala 38:16]
      rd_enable <= UInt<1>("h01") @[MaskedSmemStruct.scala 39:15]
      skip @[MaskedSmemStruct.scala 38:16]
    wire _WIRE : UInt @[MaskedSmemStruct.scala 41:28]
    _WIRE is invalid @[MaskedSmemStruct.scala 41:28]
    when rd_enable : @[MaskedSmemStruct.scala 41:28]
      _WIRE <= io.addr @[MaskedSmemStruct.scala 41:28]
      node _T_1 = or(_WIRE, UInt<10>("h00")) @[MaskedSmemStruct.scala 41:28]
      node _T_2 = bits(_T_1, 9, 0) @[MaskedSmemStruct.scala 41:28]
      read mport MPORT_1 = my_mem[_T_2], clock @[MaskedSmemStruct.scala 41:28]
      skip @[MaskedSmemStruct.scala 41:28]
    io.dataOut[0].yy <= MPORT_1[0].yy @[MaskedSmemStruct.scala 41:14]
    io.dataOut[0].xx.d2 <= MPORT_1[0].xx.d2 @[MaskedSmemStruct.scala 41:14]
    io.dataOut[0].xx.d1 <= MPORT_1[0].xx.d1 @[MaskedSmemStruct.scala 41:14]
    io.dataOut[1].yy <= MPORT_1[1].yy @[MaskedSmemStruct.scala 41:14]
    io.dataOut[1].xx.d2 <= MPORT_1[1].xx.d2 @[MaskedSmemStruct.scala 41:14]
    io.dataOut[1].xx.d1 <= MPORT_1[1].xx.d1 @[MaskedSmemStruct.scala 41:14]
    io.dataOut[2].yy <= MPORT_1[2].yy @[MaskedSmemStruct.scala 41:14]
    io.dataOut[2].xx.d2 <= MPORT_1[2].xx.d2 @[MaskedSmemStruct.scala 41:14]
    io.dataOut[2].xx.d1 <= MPORT_1[2].xx.d1 @[MaskedSmemStruct.scala 41:14]
    io.dataOut[3].yy <= MPORT_1[3].yy @[MaskedSmemStruct.scala 41:14]
    io.dataOut[3].xx.d2 <= MPORT_1[3].xx.d2 @[MaskedSmemStruct.scala 41:14]
    io.dataOut[3].xx.d1 <= MPORT_1[3].xx.d1 @[MaskedSmemStruct.scala 41:14]
    
