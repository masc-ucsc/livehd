circuit MemoryController :
  module MemoryController :
    input clock : Clock
    input reset : UInt<1>
    output io : { ddr3 : { casN : UInt<1>, rasN : UInt<1>, ba : UInt<3>}}

    wire resetDDR3Cmd : { casN : UInt<1>, rasN : UInt<1>, ba : UInt<3>} @[MemoryController.scala 24:26]
    resetDDR3Cmd.casN <= UInt<1>("h1") @[MemoryController.scala 12:10]
    resetDDR3Cmd.rasN <= UInt<1>("h1") @[MemoryController.scala 13:10]
    resetDDR3Cmd.ba <= UInt<1>("h0") @[MemoryController.scala 14:10]
    reg nextDDR3Cmd : { casN : UInt<1>, rasN : UInt<1>, ba : UInt<3>}, clock with :
      reset => (reset, resetDDR3Cmd) @[MemoryController.scala 27:28]
    when UInt<1>("h1") : @[MemoryController.scala 30:17]
      nextDDR3Cmd.ba <= UInt<2>("h3") @[MemoryController.scala 31:22]
      nextDDR3Cmd.casN <= UInt<1>("h0") @[MemoryController.scala 32:22]
      skip
    io.ddr3.ba <= nextDDR3Cmd.ba @[MemoryController.scala 36:11]
    io.ddr3.rasN <= nextDDR3Cmd.rasN @[MemoryController.scala 36:11]
    io.ddr3.casN <= nextDDR3Cmd.casN @[MemoryController.scala 36:11]