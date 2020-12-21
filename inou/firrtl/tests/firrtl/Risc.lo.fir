circuit Risc :
  module Risc :
    input clock : Clock
    input reset : UInt<1>
    input io_isWr : UInt<1>
    input io_wrAddr : UInt<8>
    input io_wrData : UInt<32>
    input io_boot : UInt<1>
    output io_valid : UInt<1>
    output io_out : UInt<32>

    mem file : @[Risc.scala 16:17]
      data-type => UInt<32>
      depth => 256
      read-latency => 0
      write-latency => 1
      reader => _T_1
      reader => _T_3
      writer => _T_12
      read-under-write => undefined
    mem code : @[Risc.scala 17:17]
      data-type => UInt<32>
      depth => 256
      read-latency => 0
      write-latency => 1
      reader => inst
      writer => _T_4
      read-under-write => undefined
    reg pc : UInt<8>, clock with :
      reset => (UInt<1>("h0"), pc) @[Risc.scala 18:21]
    node op = bits(code.inst.data, 31, 24) @[Risc.scala 23:18]
    node rci = bits(code.inst.data, 23, 16) @[Risc.scala 24:18]
    node rai = bits(code.inst.data, 15, 8) @[Risc.scala 25:18]
    node rbi = bits(code.inst.data, 7, 0) @[Risc.scala 26:18]
    node _T = eq(rai, UInt<1>("h0")) @[Risc.scala 28:20]
    node ra = mux(_T, UInt<1>("h0"), file._T_1.data) @[Risc.scala 28:15]
    node _T_2 = eq(rbi, UInt<1>("h0")) @[Risc.scala 29:20]
    node rb = mux(_T_2, UInt<1>("h0"), file._T_3.data) @[Risc.scala 29:15]
    node _T_5 = eq(UInt<1>("h0"), op) @[Conditional.scala 37:30]
    node _T_6 = add(ra, rb) @[Risc.scala 42:29]
    node _T_7 = tail(_T_6, 1) @[Risc.scala 42:29]
    node _T_8 = eq(UInt<1>("h1"), op) @[Conditional.scala 37:30]
    node _T_9 = dshl(rai, UInt<4>("h8")) @[Risc.scala 43:31]
    node _T_10 = or(_T_9, rbi) @[Risc.scala 43:39]
    node _GEN_0 = mux(_T_8, _T_10, UInt<1>("h0")) @[Conditional.scala 39:67 Risc.scala 43:23 Risc.scala 34:12]
    node _GEN_1 = mux(_T_5, _T_7, _GEN_0) @[Conditional.scala 40:58 Risc.scala 42:23]
    node _T_11 = eq(rci, UInt<8>("hff")) @[Risc.scala 46:15]
    node _GEN_2 = mux(_T_11, UInt<1>("h1"), UInt<1>("h0")) @[Risc.scala 46:26 Risc.scala 47:16 Risc.scala 32:12]
    node _GEN_3 = validif(eq(_T_11, UInt<1>("h0")), rci) @[Risc.scala 46:26 Risc.scala 49:11]
    node _GEN_4 = validif(eq(_T_11, UInt<1>("h0")), clock) @[Risc.scala 46:26 Risc.scala 49:11]
    node _GEN_5 = mux(_T_11, UInt<1>("h0"), UInt<1>("h1")) @[Risc.scala 46:26 Risc.scala 16:17 Risc.scala 49:11]
    node _GEN_6 = validif(eq(_T_11, UInt<1>("h0")), UInt<1>("h1")) @[Risc.scala 46:26 Risc.scala 49:17]
    node _GEN_9 = mux(io_boot, UInt<1>("h0"), _GEN_1) @[Risc.scala 38:25 Risc.scala 34:12]
    node _GEN_23 = mux(io_isWr, UInt<1>("h0"), _GEN_9) @[Risc.scala 36:18 Risc.scala 34:12]
    node rc = _GEN_23 @[Risc.scala 30:16]
    node _GEN_7 = validif(eq(_T_11, UInt<1>("h0")), rc) @[Risc.scala 46:26 Risc.scala 49:17]
    node _T_13 = add(pc, UInt<1>("h1")) @[Risc.scala 51:14]
    node _T_14 = tail(_T_13, 1) @[Risc.scala 51:14]
    node _GEN_8 = mux(io_boot, UInt<1>("h0"), _T_14) @[Risc.scala 38:25 Risc.scala 39:8 Risc.scala 51:8]
    node _GEN_10 = mux(io_boot, UInt<1>("h0"), rc) @[Risc.scala 38:25 Risc.scala 33:12 Risc.scala 45:12]
    node _GEN_11 = mux(io_boot, UInt<1>("h0"), _GEN_2) @[Risc.scala 38:25 Risc.scala 32:12]
    node _GEN_12 = validif(eq(io_boot, UInt<1>("h0")), _GEN_3) @[Risc.scala 38:25]
    node _GEN_13 = validif(eq(io_boot, UInt<1>("h0")), _GEN_4) @[Risc.scala 38:25]
    node _GEN_14 = mux(io_boot, UInt<1>("h0"), _GEN_5) @[Risc.scala 38:25 Risc.scala 16:17]
    node _GEN_15 = validif(eq(io_boot, UInt<1>("h0")), _GEN_6) @[Risc.scala 38:25]
    node _GEN_16 = validif(eq(io_boot, UInt<1>("h0")), _GEN_7) @[Risc.scala 38:25]
    node _GEN_17 = validif(io_isWr, io_wrAddr) @[Risc.scala 36:18 Risc.scala 37:9]
    node _GEN_18 = validif(io_isWr, clock) @[Risc.scala 36:18 Risc.scala 37:9]
    node _GEN_19 = mux(io_isWr, UInt<1>("h1"), UInt<1>("h0")) @[Risc.scala 36:18 Risc.scala 37:9 Risc.scala 17:17]
    node _GEN_20 = validif(io_isWr, UInt<1>("h1")) @[Risc.scala 36:18 Risc.scala 37:21]
    node _GEN_21 = validif(io_isWr, io_wrData) @[Risc.scala 36:18 Risc.scala 37:21]
    node _GEN_22 = mux(io_isWr, pc, _GEN_8) @[Risc.scala 36:18 Risc.scala 18:21]
    node _GEN_24 = mux(io_isWr, UInt<1>("h0"), _GEN_10) @[Risc.scala 36:18 Risc.scala 33:12]
    node _GEN_25 = mux(io_isWr, UInt<1>("h0"), _GEN_11) @[Risc.scala 36:18 Risc.scala 32:12]
    node _GEN_26 = validif(eq(io_isWr, UInt<1>("h0")), _GEN_12) @[Risc.scala 36:18]
    node _GEN_27 = validif(eq(io_isWr, UInt<1>("h0")), _GEN_13) @[Risc.scala 36:18]
    node _GEN_28 = mux(io_isWr, UInt<1>("h0"), _GEN_14) @[Risc.scala 36:18 Risc.scala 16:17]
    node _GEN_29 = validif(eq(io_isWr, UInt<1>("h0")), _GEN_15) @[Risc.scala 36:18]
    node _GEN_30 = validif(eq(io_isWr, UInt<1>("h0")), _GEN_16) @[Risc.scala 36:18]
    io_valid <= _GEN_25
    io_out <= _GEN_24
    file._T_1.addr <= rai @[Risc.scala 28:38]
    file._T_1.en <= UInt<1>("h1") @[Risc.scala 28:38]
    file._T_1.clk <= clock @[Risc.scala 28:38]
    file._T_3.addr <= rbi @[Risc.scala 29:38]
    file._T_3.en <= UInt<1>("h1") @[Risc.scala 29:38]
    file._T_3.clk <= clock @[Risc.scala 29:38]
    file._T_12.addr <= _GEN_26
    file._T_12.en <= _GEN_28
    file._T_12.clk <= _GEN_27
    file._T_12.data <= _GEN_30
    file._T_12.mask <= _GEN_29
    code.inst.addr <= pc @[Risc.scala 22:18]
    code.inst.en <= UInt<1>("h1") @[Risc.scala 22:18]
    code.inst.clk <= clock @[Risc.scala 22:18]
    code._T_4.addr <= _GEN_17
    code._T_4.en <= _GEN_19
    code._T_4.clk <= _GEN_18
    code._T_4.data <= _GEN_21
    code._T_4.mask <= _GEN_20
    pc <= mux(reset, UInt<8>("h0"), _GEN_22) @[Risc.scala 18:21 Risc.scala 18:21]