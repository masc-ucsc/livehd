circuit DynamicMemorySearch :
  module DynamicMemorySearch :
    input clock : Clock
    input reset : UInt<1>
    input io_isWr : UInt<1>
    input io_wrAddr : UInt<3>
    input io_data : UInt<4>
    input io_en : UInt<1>
    output io_target : UInt<3>
    output io_done : UInt<1>

    mem list : @[DynamicMemorySearch.scala 32:19]
      data-type => UInt<4>
      depth => 8
      read-latency => 0
      write-latency => 1
      reader => memVal
      writer => _T_4
      read-under-write => undefined
    reg index : UInt<3>, clock with :
      reset => (UInt<1>("h0"), index) @[DynamicMemorySearch.scala 30:23]
    node _T = eq(io_en, UInt<1>("h0")) @[DynamicMemorySearch.scala 35:16]
    node _T_1 = eq(list.memVal.data, io_data) @[DynamicMemorySearch.scala 35:35]
    node _T_2 = eq(index, UInt<3>("h7")) @[DynamicMemorySearch.scala 35:58]
    node _T_3 = or(_T_1, _T_2) @[DynamicMemorySearch.scala 35:48]
    node done = and(_T, _T_3) @[DynamicMemorySearch.scala 35:23]
    node _T_5 = eq(done, UInt<1>("h0")) @[DynamicMemorySearch.scala 41:21]
    node _T_6 = add(index, UInt<1>("h1")) @[DynamicMemorySearch.scala 42:20]
    node _T_7 = tail(_T_6, 1) @[DynamicMemorySearch.scala 42:20]
    node _GEN_0 = mux(_T_5, _T_7, index) @[DynamicMemorySearch.scala 41:34 DynamicMemorySearch.scala 42:11 DynamicMemorySearch.scala 30:23]
    node _GEN_1 = mux(io_en, UInt<1>("h0"), _GEN_0) @[DynamicMemorySearch.scala 39:23 DynamicMemorySearch.scala 40:11]
    node _GEN_2 = validif(io_isWr, io_wrAddr) @[DynamicMemorySearch.scala 37:18 DynamicMemorySearch.scala 38:9]
    node _GEN_3 = validif(io_isWr, clock) @[DynamicMemorySearch.scala 37:18 DynamicMemorySearch.scala 38:9]
    node _GEN_4 = mux(io_isWr, UInt<1>("h1"), UInt<1>("h0")) @[DynamicMemorySearch.scala 37:18 DynamicMemorySearch.scala 38:9 DynamicMemorySearch.scala 32:19]
    node _GEN_5 = validif(io_isWr, UInt<1>("h1")) @[DynamicMemorySearch.scala 37:18 DynamicMemorySearch.scala 38:21]
    node _GEN_6 = validif(io_isWr, io_data) @[DynamicMemorySearch.scala 37:18 DynamicMemorySearch.scala 38:21]
    node _GEN_7 = mux(io_isWr, index, _GEN_1) @[DynamicMemorySearch.scala 37:18 DynamicMemorySearch.scala 30:23]
    io_target <= index @[DynamicMemorySearch.scala 45:13]
    io_done <= done @[DynamicMemorySearch.scala 44:13]
    index <= mux(reset, UInt<3>("h0"), _GEN_7) @[DynamicMemorySearch.scala 30:23 DynamicMemorySearch.scala 30:23]
    list.memVal.addr <= index @[DynamicMemorySearch.scala 33:20]
    list.memVal.en <= UInt<1>("h1") @[DynamicMemorySearch.scala 33:20]
    list.memVal.clk <= clock @[DynamicMemorySearch.scala 33:20]
    list._T_4.addr <= _GEN_2
    list._T_4.en <= _GEN_4
    list._T_4.clk <= _GEN_3
    list._T_4.data <= _GEN_6
    list._T_4.mask <= _GEN_5