circuit SmemStruct :
  module SmemStruct :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip enable : UInt<1>, flip write : UInt<1>, flip addr : UInt<10>, flip dataIn : { x_data : UInt<8>, y_data : UInt<8>}, dataOut : { x_data : UInt<8>, y_data : UInt<8>}}

    mem my_mem : @[SmemStruct.scala 22:27]
      data-type => { x_data : UInt<8>, y_data : UInt<8>}
      depth => 1024
      read-latency => 1
      write-latency => 1
      reader => io_dataOut_MPORT
      writer => MPORT
      read-under-write => undefined
    my_mem.io_dataOut_MPORT.addr is invalid @[SmemStruct.scala 22:27]
    my_mem.io_dataOut_MPORT.clk is invalid @[SmemStruct.scala 22:27]
    my_mem.io_dataOut_MPORT.en <= UInt<1>("h0") @[SmemStruct.scala 22:27]
    my_mem.MPORT.addr is invalid @[SmemStruct.scala 22:27]
    my_mem.MPORT.clk is invalid @[SmemStruct.scala 22:27]
    my_mem.MPORT.en <= UInt<1>("h0") @[SmemStruct.scala 22:27]
    my_mem.MPORT.data is invalid @[SmemStruct.scala 22:27]
    my_mem.MPORT.mask is invalid @[SmemStruct.scala 22:27]
    my_mem.MPORT.addr <= io.addr
    my_mem.MPORT.clk <= clock
    my_mem.MPORT.en <= UInt<1>("h1")
    my_mem.MPORT.mask.x_data <= UInt<1>("h0")
    my_mem.MPORT.mask.y_data <= UInt<1>("h0")
    my_mem.MPORT.data.y_data <= io.dataIn.y_data
    my_mem.MPORT.mask.y_data <= UInt<1>("h1")
    my_mem.MPORT.data.x_data <= io.dataIn.x_data
    my_mem.MPORT.mask.x_data <= UInt<1>("h1")
    wire _io_dataOut_WIRE : UInt<10> @[SmemStruct.scala 25:28]
    _io_dataOut_WIRE is invalid @[SmemStruct.scala 25:28]
    when io.enable : @[SmemStruct.scala 25:28]
      _io_dataOut_WIRE <= io.addr @[SmemStruct.scala 25:28]
      node _io_dataOut_T = or(_io_dataOut_WIRE, UInt<10>("h0")) @[SmemStruct.scala 25:28]
      node _io_dataOut_T_1 = bits(_io_dataOut_T, 9, 0) @[SmemStruct.scala 25:28]
      my_mem.io_dataOut_MPORT.en <= UInt<1>("h1") @[SmemStruct.scala 25:28]
      my_mem.io_dataOut_MPORT.addr <= _io_dataOut_T_1 @[SmemStruct.scala 25:28]
      my_mem.io_dataOut_MPORT.clk <= clock @[SmemStruct.scala 25:28]
    io.dataOut.y_data <= my_mem.io_dataOut_MPORT.data.y_data @[SmemStruct.scala 25:14]
    io.dataOut.x_data <= my_mem.io_dataOut_MPORT.data.x_data @[SmemStruct.scala 25:14]
