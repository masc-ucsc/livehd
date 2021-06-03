;buildInfoPackage: chisel3, version: 3.4.2, scalaVersion: 2.12.12, sbtVersion: 1.3.10
circuit MaskedSmemStruct : 
  module MaskedSmemStruct : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip enable : UInt<1>, flip write : UInt<1>, flip addr : UInt<10>, flip mask : UInt<1>[4], flip dataIn : {x_data : UInt<8>, y_data : UInt<8>}[4], dataOut : {x_data : UInt<8>, y_data : UInt<8>}[4]}
    
    smem my_mem : {x_data : UInt<8>, y_data : UInt<8>}[4][1024], undefined @[MaskedSmemStruct.scala 24:27]
    write mport MPORT = my_mem[io.addr], clock
    when io.mask[0] :
      MPORT[0].y_data <= io.dataIn[0].y_data
      MPORT[0].x_data <= io.dataIn[0].x_data
      skip
    when io.mask[1] :
      MPORT[1].y_data <= io.dataIn[1].y_data
      MPORT[1].x_data <= io.dataIn[1].x_data
      skip
    when io.mask[2] :
      MPORT[2].y_data <= io.dataIn[2].y_data
      MPORT[2].x_data <= io.dataIn[2].x_data
      skip
    when io.mask[3] :
      MPORT[3].y_data <= io.dataIn[3].y_data
      MPORT[3].x_data <= io.dataIn[3].x_data
      skip
    wire rd_enable : UInt<1> @[MaskedSmemStruct.scala 29:23]
    node _T = gt(io.addr, UInt<6>("h020")) @[MaskedSmemStruct.scala 30:17]
    when _T : @[MaskedSmemStruct.scala 30:25]
      rd_enable <= io.enable @[MaskedSmemStruct.scala 31:15]
      skip @[MaskedSmemStruct.scala 30:25]
    else : @[MaskedSmemStruct.scala 32:16]
      rd_enable <= UInt<1>("h01") @[MaskedSmemStruct.scala 33:15]
      skip @[MaskedSmemStruct.scala 32:16]
    wire _WIRE : UInt @[MaskedSmemStruct.scala 35:28]
    _WIRE is invalid @[MaskedSmemStruct.scala 35:28]
    when rd_enable : @[MaskedSmemStruct.scala 35:28]
      _WIRE <= io.addr @[MaskedSmemStruct.scala 35:28]
      node _T_1 = or(_WIRE, UInt<10>("h00")) @[MaskedSmemStruct.scala 35:28]
      node _T_2 = bits(_T_1, 9, 0) @[MaskedSmemStruct.scala 35:28]
      read mport MPORT_1 = my_mem[_T_2], clock @[MaskedSmemStruct.scala 35:28]
      skip @[MaskedSmemStruct.scala 35:28]
    io.dataOut[0].y_data <= MPORT_1[0].y_data @[MaskedSmemStruct.scala 35:14]
    io.dataOut[0].x_data <= MPORT_1[0].x_data @[MaskedSmemStruct.scala 35:14]
    io.dataOut[1].y_data <= MPORT_1[1].y_data @[MaskedSmemStruct.scala 35:14]
    io.dataOut[1].x_data <= MPORT_1[1].x_data @[MaskedSmemStruct.scala 35:14]
    io.dataOut[2].y_data <= MPORT_1[2].y_data @[MaskedSmemStruct.scala 35:14]
    io.dataOut[2].x_data <= MPORT_1[2].x_data @[MaskedSmemStruct.scala 35:14]
    io.dataOut[3].y_data <= MPORT_1[3].y_data @[MaskedSmemStruct.scala 35:14]
    io.dataOut[3].x_data <= MPORT_1[3].x_data @[MaskedSmemStruct.scala 35:14]
    
