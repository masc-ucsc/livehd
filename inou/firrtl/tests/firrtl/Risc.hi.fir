circuit Risc :
  module Risc :
    input clock : Clock
    input reset : UInt<1>
    output io : { flip isWr : UInt<1>, flip wrAddr : UInt<8>, flip wrData : UInt<32>, flip boot : UInt<1>, valid : UInt<1>, out : UInt<32>}

    mem file : @[Risc.scala 16:17]
      data-type => UInt<32>
      depth => 256
      read-latency => 0
      write-latency => 1
      reader => _T_1
      reader => _T_3
      writer => _T_12
      read-under-write => undefined
    file._T_1.addr is invalid @[Risc.scala 16:17]
    file._T_1.clk is invalid @[Risc.scala 16:17]
    file._T_3.addr is invalid @[Risc.scala 16:17]
    file._T_3.clk is invalid @[Risc.scala 16:17]
    file._T_1.en <= UInt<1>("h0") @[Risc.scala 16:17]
    file._T_3.en <= UInt<1>("h0") @[Risc.scala 16:17]
    file._T_12.addr is invalid @[Risc.scala 16:17]
    file._T_12.clk is invalid @[Risc.scala 16:17]
    file._T_12.en <= UInt<1>("h0") @[Risc.scala 16:17]
    file._T_12.data is invalid @[Risc.scala 16:17]
    file._T_12.mask is invalid @[Risc.scala 16:17]
    mem code : @[Risc.scala 17:17]
      data-type => UInt<32>
      depth => 256
      read-latency => 0
      write-latency => 1
      reader => inst
      writer => _T_4
      read-under-write => undefined
    code.inst.addr is invalid @[Risc.scala 17:17]
    code.inst.clk is invalid @[Risc.scala 17:17]
    code.inst.en <= UInt<1>("h0") @[Risc.scala 17:17]
    code._T_4.addr is invalid @[Risc.scala 17:17]
    code._T_4.clk is invalid @[Risc.scala 17:17]
    code._T_4.en <= UInt<1>("h0") @[Risc.scala 17:17]
    code._T_4.data is invalid @[Risc.scala 17:17]
    code._T_4.mask is invalid @[Risc.scala 17:17]
    reg pc : UInt<8>, clock with :
      reset => (reset, UInt<8>("h0")) @[Risc.scala 18:21]
    code.inst.addr <= pc @[Risc.scala 22:18]
    code.inst.clk <= clock @[Risc.scala 22:18]
    code.inst.en <= UInt<1>("h1") @[Risc.scala 22:18]
    node op = bits(code.inst.data, 31, 24) @[Risc.scala 23:18]
    node rci = bits(code.inst.data, 23, 16) @[Risc.scala 24:18]
    node rai = bits(code.inst.data, 15, 8) @[Risc.scala 25:18]
    node rbi = bits(code.inst.data, 7, 0) @[Risc.scala 26:18]
    node _T = eq(rai, UInt<1>("h0")) @[Risc.scala 28:20]
    file._T_1.addr <= rai @[Risc.scala 28:38]
    file._T_1.clk <= clock @[Risc.scala 28:38]
    file._T_1.en <= UInt<1>("h1") @[Risc.scala 28:38]
    node ra = mux(_T, UInt<1>("h0"), file._T_1.data) @[Risc.scala 28:15]
    node _T_2 = eq(rbi, UInt<1>("h0")) @[Risc.scala 29:20]
    file._T_3.addr <= rbi @[Risc.scala 29:38]
    file._T_3.clk <= clock @[Risc.scala 29:38]
    file._T_3.en <= UInt<1>("h1") @[Risc.scala 29:38]
    node rb = mux(_T_2, UInt<1>("h0"), file._T_3.data) @[Risc.scala 29:15]
    wire rc : UInt<32> @[Risc.scala 30:16]
    io.valid <= UInt<1>("h0") @[Risc.scala 32:12]
    io.out <= UInt<1>("h0") @[Risc.scala 33:12]
    rc <= UInt<1>("h0") @[Risc.scala 34:12]
    when io.isWr : @[Risc.scala 36:18]
      code._T_4.addr <= io.wrAddr @[Risc.scala 37:9]
      code._T_4.clk <= clock @[Risc.scala 37:9]
      code._T_4.en <= UInt<1>("h1") @[Risc.scala 37:9]
      code._T_4.mask <= UInt<1>("h0") @[Risc.scala 37:9]
      code._T_4.data <= io.wrData @[Risc.scala 37:21]
      code._T_4.mask <= UInt<1>("h1") @[Risc.scala 37:21]
    else :
      when io.boot : @[Risc.scala 38:25]
        pc <= UInt<1>("h0") @[Risc.scala 39:8]
      else :
        node _T_5 = eq(UInt<1>("h0"), op) @[Conditional.scala 37:30]
        when _T_5 : @[Conditional.scala 40:58]
          node _T_6 = add(ra, rb) @[Risc.scala 42:29]
          node _T_7 = tail(_T_6, 1) @[Risc.scala 42:29]
          rc <= _T_7 @[Risc.scala 42:23]
        else :
          node _T_8 = eq(UInt<1>("h1"), op) @[Conditional.scala 37:30]
          when _T_8 : @[Conditional.scala 39:67]
            node _T_9 = dshl(rai, UInt<4>("h8")) @[Risc.scala 43:31]
            node _T_10 = or(_T_9, rbi) @[Risc.scala 43:39]
            rc <= _T_10 @[Risc.scala 43:23]
        io.out <= rc @[Risc.scala 45:12]
        node _T_11 = eq(rci, UInt<8>("hff")) @[Risc.scala 46:15]
        when _T_11 : @[Risc.scala 46:26]
          io.valid <= UInt<1>("h1") @[Risc.scala 47:16]
        else :
          file._T_12.addr <= rci @[Risc.scala 49:11]
          file._T_12.clk <= clock @[Risc.scala 49:11]
          file._T_12.en <= UInt<1>("h1") @[Risc.scala 49:11]
          file._T_12.mask <= UInt<1>("h0") @[Risc.scala 49:11]
          file._T_12.data <= rc @[Risc.scala 49:17]
          file._T_12.mask <= UInt<1>("h1") @[Risc.scala 49:17]
        node _T_13 = add(pc, UInt<1>("h1")) @[Risc.scala 51:14]
        node _T_14 = tail(_T_13, 1) @[Risc.scala 51:14]
        pc <= _T_14 @[Risc.scala 51:8]