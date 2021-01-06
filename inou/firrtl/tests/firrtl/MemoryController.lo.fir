circuit MemoryController :
  module MemoryController :
    input clock : Clock
    input reset : UInt<1>
    output io_ddr3_casN : UInt<1>
    output io_ddr3_rasN : UInt<1>
    output io_ddr3_ba : UInt<3>

    reg nextDDR3Cmd_casN : UInt<1>, clock with :
      reset => (UInt<1>("h0"), nextDDR3Cmd_casN) @[MemoryController.scala 27:28]
    reg nextDDR3Cmd_rasN : UInt<1>, clock with :
      reset => (UInt<1>("h0"), nextDDR3Cmd_rasN) @[MemoryController.scala 27:28]
    reg nextDDR3Cmd_ba : UInt<3>, clock with :
      reset => (UInt<1>("h0"), nextDDR3Cmd_ba) @[MemoryController.scala 27:28]
    node _GEN_0 = mux(UInt<1>("h1"), UInt<2>("h3"), nextDDR3Cmd_ba) @[MemoryController.scala 30:17 MemoryController.scala 31:22 MemoryController.scala 27:28]
    node _GEN_1 = mux(UInt<1>("h1"), UInt<1>("h0"), nextDDR3Cmd_casN) @[MemoryController.scala 30:17 MemoryController.scala 32:22 MemoryController.scala 27:28]
    node resetDDR3Cmd_casN = UInt<1>("h1") @[MemoryController.scala 24:26 MemoryController.scala 12:10]
    node resetDDR3Cmd_rasN = UInt<1>("h1") @[MemoryController.scala 24:26 MemoryController.scala 13:10]
    node resetDDR3Cmd_ba = pad(UInt<1>("h0"), 3) @[MemoryController.scala 24:26 MemoryController.scala 14:10]
    io_ddr3_casN <= nextDDR3Cmd_casN @[MemoryController.scala 36:11]
    io_ddr3_rasN <= nextDDR3Cmd_rasN @[MemoryController.scala 36:11]
    io_ddr3_ba <= nextDDR3Cmd_ba @[MemoryController.scala 36:11]
    nextDDR3Cmd_casN <= mux(reset, resetDDR3Cmd_casN, _GEN_1) @[MemoryController.scala 27:28 MemoryController.scala 27:28]
    nextDDR3Cmd_rasN <= mux(reset, resetDDR3Cmd_rasN, nextDDR3Cmd_rasN) @[MemoryController.scala 27:28 MemoryController.scala 27:28 MemoryController.scala 27:28]
    nextDDR3Cmd_ba <= mux(reset, resetDDR3Cmd_ba, _GEN_0) @[MemoryController.scala 27:28 MemoryController.scala 27:28]