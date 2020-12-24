circuit Memo :
  module Memo :
    input clock : Clock
    input reset : UInt<1>
    input io_wen : UInt<1>
    input io_wrAddr : UInt<8>
    input io_wrData : UInt<8>
    input io_ren : UInt<1>
    input io_rdAddr : UInt<8>
    output io_rdData : UInt<8>

    mem mem : @[Memo.scala 23:16]
      data-type => UInt<8>
      depth => 256
      read-latency => 0
      write-latency => 1
      reader => _T_1
      writer => _T
      read-under-write => undefined
    node _GEN_0 = validif(io_wen, io_wrAddr) @[Memo.scala 26:17 Memo.scala 26:22]
    node _GEN_1 = validif(io_wen, clock) @[Memo.scala 26:17 Memo.scala 26:22]
    node _GEN_2 = mux(io_wen, UInt<1>("h1"), UInt<1>("h0")) @[Memo.scala 26:17 Memo.scala 26:22 Memo.scala 23:16]
    node _GEN_3 = validif(io_wen, UInt<1>("h1")) @[Memo.scala 26:17 Memo.scala 26:34]
    node _GEN_4 = validif(io_wen, io_wrData) @[Memo.scala 26:17 Memo.scala 26:34]
    node _GEN_5 = validif(io_ren, io_rdAddr) @[Memo.scala 30:17 Memo.scala 30:35]
    node _GEN_6 = validif(io_ren, clock) @[Memo.scala 30:17 Memo.scala 30:35]
    node _GEN_7 = mux(io_ren, UInt<1>("h1"), UInt<1>("h0")) @[Memo.scala 30:17 Memo.scala 30:35 Memo.scala 23:16]
    node _GEN_8 = mux(io_ren, mem._T_1.data, UInt<1>("h0")) @[Memo.scala 30:17 Memo.scala 30:29 Memo.scala 29:13]
    io_rdData <= _GEN_8
    mem._T_1.addr <= _GEN_5
    mem._T_1.en <= _GEN_7
    mem._T_1.clk <= _GEN_6
    mem._T.addr <= _GEN_0
    mem._T.en <= _GEN_2
    mem._T.clk <= _GEN_1
    mem._T.data <= _GEN_4
    mem._T.mask <= _GEN_3