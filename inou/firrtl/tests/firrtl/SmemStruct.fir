;buildInfoPackage: chisel3, version: 3.4.2, scalaVersion: 2.12.12, sbtVersion: 1.3.10
circuit SmemStruct : 
  module SmemStruct : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip enable : UInt<1>, flip write : UInt<1>, flip addr : UInt<10>, flip dataIn : {xx : {d1 : UInt<2>, d2 : UInt<2>}, yy : UInt<4>}, dataOut : {xx : {d1 : UInt<2>, d2 : UInt<2>}, yy : UInt<4>}}
    
    smem my_mem : {xx : {d1 : UInt<2>, d2 : UInt<2>}, yy : UInt<4>}[1024], undefined @[SmemStruct.scala 29:27]
    write mport MPORT = my_mem[io.addr], clock
    MPORT.yy <= io.dataIn.yy
    MPORT.xx.d2 <= io.dataIn.xx.d2
    MPORT.xx.d1 <= io.dataIn.xx.d1
    wire rd_enable : UInt<1> @[SmemStruct.scala 35:23]
    node _T = gt(io.addr, UInt<6>("h020")) @[SmemStruct.scala 36:17]
    when _T : @[SmemStruct.scala 36:25]
      rd_enable <= io.enable @[SmemStruct.scala 37:15]
      skip @[SmemStruct.scala 36:25]
    else : @[SmemStruct.scala 38:16]
      rd_enable <= UInt<1>("h01") @[SmemStruct.scala 39:15]
      skip @[SmemStruct.scala 38:16]
    wire _io_dataOut_WIRE : UInt @[SmemStruct.scala 41:28]
    _io_dataOut_WIRE is invalid @[SmemStruct.scala 41:28]
    when rd_enable : @[SmemStruct.scala 41:28]
      _io_dataOut_WIRE <= io.addr @[SmemStruct.scala 41:28]
      node _io_dataOut_T = or(_io_dataOut_WIRE, UInt<10>("h00")) @[SmemStruct.scala 41:28]
      node _io_dataOut_T_1 = bits(_io_dataOut_T, 9, 0) @[SmemStruct.scala 41:28]
      read mport io_dataOut_MPORT = my_mem[_io_dataOut_T_1], clock @[SmemStruct.scala 41:28]
      skip @[SmemStruct.scala 41:28]
    io.dataOut.yy <= io_dataOut_MPORT.yy @[SmemStruct.scala 41:14]
    io.dataOut.xx.d2 <= io_dataOut_MPORT.xx.d2 @[SmemStruct.scala 41:14]
    io.dataOut.xx.d1 <= io_dataOut_MPORT.xx.d1 @[SmemStruct.scala 41:14]
    
