circuit Stack :
  module Stack :
    input clock : Clock
    input reset : UInt<1>
    input io_push : UInt<1>
    input io_pop : UInt<1>
    input io_en : UInt<1>
    input io_dataIn : UInt<32>
    output io_dataOut : UInt<32>

    mem stack_mem : @[Stack.scala 16:22]
      data-type => UInt<32>
      depth => 8
      read-latency => 0
      write-latency => 1
      reader => _T_14
      writer => _T_3
      read-under-write => undefined
    reg sp : UInt<4>, clock with :
      reset => (UInt<1>("h0"), sp) @[Stack.scala 17:26]
    reg out : UInt<32>, clock with :
      reset => (UInt<1>("h0"), out) @[Stack.scala 18:26]
    node _T = lt(sp, UInt<4>("h8")) @[Stack.scala 21:25]
    node _T_1 = and(io_push, _T) @[Stack.scala 21:18]
    node _T_2 = bits(sp, 2, 0) @[Stack.scala 22:16]
    node _T_4 = add(sp, UInt<1>("h1")) @[Stack.scala 23:16]
    node _T_5 = tail(_T_4, 1) @[Stack.scala 23:16]
    node _T_6 = gt(sp, UInt<1>("h0")) @[Stack.scala 24:31]
    node _T_7 = and(io_pop, _T_6) @[Stack.scala 24:24]
    node _T_8 = sub(sp, UInt<1>("h1")) @[Stack.scala 25:16]
    node _T_9 = tail(_T_8, 1) @[Stack.scala 25:16]
    node _GEN_0 = mux(_T_7, _T_9, sp) @[Stack.scala 24:39 Stack.scala 25:10 Stack.scala 17:26]
    node _GEN_1 = validif(_T_1, _T_2) @[Stack.scala 21:42 Stack.scala 22:16]
    node _GEN_2 = validif(_T_1, clock) @[Stack.scala 21:42 Stack.scala 22:16]
    node _GEN_3 = mux(_T_1, UInt<1>("h1"), UInt<1>("h0")) @[Stack.scala 21:42 Stack.scala 22:16 Stack.scala 16:22]
    node _GEN_4 = validif(_T_1, UInt<1>("h1")) @[Stack.scala 21:42 Stack.scala 22:21]
    node _GEN_5 = validif(_T_1, io_dataIn) @[Stack.scala 21:42 Stack.scala 22:21]
    node _GEN_6 = mux(_T_1, _T_5, _GEN_0) @[Stack.scala 21:42 Stack.scala 23:10]
    node _T_10 = gt(sp, UInt<1>("h0")) @[Stack.scala 27:14]
    node _T_11 = sub(sp, UInt<1>("h1")) @[Stack.scala 28:27]
    node _T_12 = tail(_T_11, 1) @[Stack.scala 28:27]
    node _T_13 = bits(_T_12, 2, 0) @[Stack.scala 28:23]
    node _GEN_7 = validif(_T_10, _T_13) @[Stack.scala 27:21 Stack.scala 28:23]
    node _GEN_8 = validif(_T_10, clock) @[Stack.scala 27:21 Stack.scala 28:23]
    node _GEN_9 = mux(_T_10, UInt<1>("h1"), UInt<1>("h0")) @[Stack.scala 27:21 Stack.scala 28:23 Stack.scala 16:22]
    node _GEN_10 = mux(_T_10, stack_mem._T_14.data, out) @[Stack.scala 27:21 Stack.scala 28:11 Stack.scala 18:26]
    node _GEN_11 = validif(io_en, _GEN_1) @[Stack.scala 20:16]
    node _GEN_12 = validif(io_en, _GEN_2) @[Stack.scala 20:16]
    node _GEN_13 = mux(io_en, _GEN_3, UInt<1>("h0")) @[Stack.scala 20:16 Stack.scala 16:22]
    node _GEN_14 = validif(io_en, _GEN_4) @[Stack.scala 20:16]
    node _GEN_15 = validif(io_en, _GEN_5) @[Stack.scala 20:16]
    node _GEN_16 = mux(io_en, _GEN_6, sp) @[Stack.scala 20:16 Stack.scala 17:26]
    node _GEN_17 = validif(io_en, _GEN_7) @[Stack.scala 20:16]
    node _GEN_18 = validif(io_en, _GEN_8) @[Stack.scala 20:16]
    node _GEN_19 = mux(io_en, _GEN_9, UInt<1>("h0")) @[Stack.scala 20:16 Stack.scala 16:22]
    node _GEN_20 = mux(io_en, _GEN_10, out) @[Stack.scala 20:16 Stack.scala 18:26]
    io_dataOut <= out @[Stack.scala 32:14]
    stack_mem._T_14.addr <= _GEN_17
    stack_mem._T_14.en <= _GEN_19
    stack_mem._T_14.clk <= _GEN_18
    stack_mem._T_3.addr <= _GEN_11
    stack_mem._T_3.en <= _GEN_13
    stack_mem._T_3.clk <= _GEN_12
    stack_mem._T_3.data <= _GEN_15
    stack_mem._T_3.mask <= _GEN_14
    sp <= mux(reset, UInt<4>("h0"), _GEN_16) @[Stack.scala 17:26 Stack.scala 17:26]
    out <= mux(reset, UInt<32>("h0"), _GEN_20) @[Stack.scala 18:26 Stack.scala 18:26]