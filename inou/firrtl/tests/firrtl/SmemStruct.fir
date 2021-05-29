;buildInfoPackage: chisel3, version: 3.4.2, scalaVersion: 2.12.12, sbtVersion: 1.3.10
circuit SmemStruct : 
  module SmemStruct : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip enable : UInt<1>, flip write : UInt<1>, flip addr : UInt<10>, flip dataIn : {x_data : UInt<8>, y_data : UInt<8>}, dataOut : {x_data : UInt<8>, y_data : UInt<8>}}
    
    smem my_mem : {x_data : UInt<8>, y_data : UInt<8>}[1024], undefined 
    write mport MPORT = my_mem[io.addr], clock
    MPORT.y_data <= io.dataIn.y_data
    MPORT.x_data <= io.dataIn.x_data
    wire rd_enable : UInt<1> 
    node _T = gt(io.addr, UInt<6>("h020")) 
    when _T : 
      rd_enable <= io.enable 
      skip 
    else : 
      rd_enable <= UInt<1>("h01") 
      skip 
    wire _io_dataOut_WIRE : UInt 
    _io_dataOut_WIRE is invalid 
    when rd_enable : 
      _io_dataOut_WIRE <= io.addr 
      node _io_dataOut_T = or(_io_dataOut_WIRE, UInt<10>("h00")) 
      node _io_dataOut_T_1 = bits(_io_dataOut_T, 9, 0) 
      read mport io_dataOut_MPORT = my_mem[_io_dataOut_T_1], clock 
      skip 
    io.dataOut.y_data <= io_dataOut_MPORT.y_data 
    io.dataOut.x_data <= io_dataOut_MPORT.x_data 
    
