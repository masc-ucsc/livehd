circuit Router :
  module Router :
    input clock : Clock
    input reset : UInt<1>
    output io_read_routing_table_request_ready : UInt<1>
    input io_read_routing_table_request_valid : UInt<1>
    input io_read_routing_table_request_bits_addr : UInt<32>
    input io_read_routing_table_response_ready : UInt<1>
    output io_read_routing_table_response_valid : UInt<1>
    output io_read_routing_table_response_bits : UInt<32>
    output io_load_routing_table_request_ready : UInt<1>
    input io_load_routing_table_request_valid : UInt<1>
    input io_load_routing_table_request_bits_addr : UInt<32>
    input io_load_routing_table_request_bits_data : UInt<32>
    output io_in_ready : UInt<1>
    input io_in_valid : UInt<1>
    input io_in_bits_header : UInt<8>
    input io_in_bits_body : UInt<64>
    input io_outs_0_ready : UInt<1>
    output io_outs_0_valid : UInt<1>
    output io_outs_0_bits_header : UInt<8>
    output io_outs_0_bits_body : UInt<64>
    input io_outs_1_ready : UInt<1>
    output io_outs_1_valid : UInt<1>
    output io_outs_1_bits_header : UInt<8>
    output io_outs_1_bits_body : UInt<64>
    input io_outs_2_ready : UInt<1>
    output io_outs_2_valid : UInt<1>
    output io_outs_2_bits_header : UInt<8>
    output io_outs_2_bits_body : UInt<64>
    input io_outs_3_ready : UInt<1>
    output io_outs_3_valid : UInt<1>
    output io_outs_3_bits_header : UInt<8>
    output io_outs_3_bits_body : UInt<64>

    mem tbl : @[Router.scala 51:18]
      data-type => UInt<3>
      depth => 15
      read-latency => 0
      write-latency => 1
      reader => MPORT
      reader => idx
      reader => MPORT_2
      writer => MPORT_1
      read-under-write => undefined
    node _T = and(io_read_routing_table_request_valid, io_read_routing_table_response_ready) @[Router.scala 65:44]
    node _T_1 = bits(io_read_routing_table_request_bits_addr, 3, 0) @[Router.scala 66:43]
    node _T_2 = bits(io_load_routing_table_request_bits_addr, 3, 0) @[Router.scala 72:8]
    node _T_3 = bits(reset, 0, 0) @[Router.scala 73:11]
    node _T_4 = eq(_T_3, UInt<1>("h0")) @[Router.scala 73:11]
    node _idx_T = bits(io_in_bits_header, 4, 0) @[Router.scala 77:29]
    node _idx_T_1 = bits(_idx_T, 3, 0) @[Router.scala 77:18]
    node _T_5 = bits(tbl.idx.data, 1, 0)
    node _GEN_0 = validif(eq(UInt<1>("h0"), _T_5), io_outs_0_ready) @[Router.scala 78:30 Router.scala 78:30]
    node _GEN_1 = validif(eq(UInt<1>("h0"), _T_5), io_outs_0_valid) @[Router.scala 78:30 Router.scala 78:30]
    node _GEN_2 = validif(eq(UInt<1>("h0"), _T_5), io_outs_0_bits_header) @[Router.scala 78:30 Router.scala 78:30]
    node _GEN_3 = validif(eq(UInt<1>("h0"), _T_5), io_outs_0_bits_body) @[Router.scala 78:30 Router.scala 78:30]
    node _GEN_4 = mux(eq(UInt<1>("h1"), _T_5), io_outs_1_ready, _GEN_0) @[Router.scala 78:30 Router.scala 78:30]
    node _GEN_5 = mux(eq(UInt<1>("h1"), _T_5), io_outs_1_valid, _GEN_1) @[Router.scala 78:30 Router.scala 78:30]
    node _GEN_6 = mux(eq(UInt<1>("h1"), _T_5), io_outs_1_bits_header, _GEN_2) @[Router.scala 78:30 Router.scala 78:30]
    node _GEN_7 = mux(eq(UInt<1>("h1"), _T_5), io_outs_1_bits_body, _GEN_3) @[Router.scala 78:30 Router.scala 78:30]
    node _GEN_8 = mux(eq(UInt<2>("h2"), _T_5), io_outs_2_ready, _GEN_4) @[Router.scala 78:30 Router.scala 78:30]
    node _GEN_9 = mux(eq(UInt<2>("h2"), _T_5), io_outs_2_valid, _GEN_5) @[Router.scala 78:30 Router.scala 78:30]
    node _GEN_10 = mux(eq(UInt<2>("h2"), _T_5), io_outs_2_bits_header, _GEN_6) @[Router.scala 78:30 Router.scala 78:30]
    node _GEN_11 = mux(eq(UInt<2>("h2"), _T_5), io_outs_2_bits_body, _GEN_7) @[Router.scala 78:30 Router.scala 78:30]
    node _GEN_12 = mux(eq(UInt<2>("h3"), _T_5), io_outs_3_ready, _GEN_8) @[Router.scala 78:30 Router.scala 78:30]
    node _GEN_13 = mux(eq(UInt<2>("h3"), _T_5), io_outs_3_valid, _GEN_9) @[Router.scala 78:30 Router.scala 78:30]
    node _GEN_14 = mux(eq(UInt<2>("h3"), _T_5), io_outs_3_bits_header, _GEN_10) @[Router.scala 78:30 Router.scala 78:30]
    node _GEN_15 = mux(eq(UInt<2>("h3"), _T_5), io_outs_3_bits_body, _GEN_11) @[Router.scala 78:30 Router.scala 78:30]
    node _T_6 = bits(tbl.idx.data, 1, 0)
    node _io_outs_T_6_valid = UInt<1>("h1") @[Decoupled.scala 47:20 Decoupled.scala 47:20]
    node _GEN_16 = mux(eq(UInt<1>("h0"), _T_6), _io_outs_T_6_valid, UInt<1>("h0")) @[Decoupled.scala 47:20 Decoupled.scala 47:20 Decoupled.scala 56:20]
    node _GEN_17 = mux(eq(UInt<1>("h1"), _T_6), _io_outs_T_6_valid, UInt<1>("h0")) @[Decoupled.scala 47:20 Decoupled.scala 47:20 Decoupled.scala 56:20]
    node _GEN_18 = mux(eq(UInt<2>("h2"), _T_6), _io_outs_T_6_valid, UInt<1>("h0")) @[Decoupled.scala 47:20 Decoupled.scala 47:20 Decoupled.scala 56:20]
    node _GEN_19 = mux(eq(UInt<2>("h3"), _T_6), _io_outs_T_6_valid, UInt<1>("h0")) @[Decoupled.scala 47:20 Decoupled.scala 47:20 Decoupled.scala 56:20]
    node _io_outs_T_6_bits_body = io_in_bits_body @[Decoupled.scala 48:19 Decoupled.scala 48:19]
    node _GEN_20 = validif(eq(UInt<1>("h0"), _T_6), _io_outs_T_6_bits_body) @[Decoupled.scala 48:19 Decoupled.scala 48:19]
    node _GEN_21 = validif(eq(UInt<1>("h1"), _T_6), _io_outs_T_6_bits_body) @[Decoupled.scala 48:19 Decoupled.scala 48:19]
    node _GEN_22 = validif(eq(UInt<2>("h2"), _T_6), _io_outs_T_6_bits_body) @[Decoupled.scala 48:19 Decoupled.scala 48:19]
    node _GEN_23 = validif(eq(UInt<2>("h3"), _T_6), _io_outs_T_6_bits_body) @[Decoupled.scala 48:19 Decoupled.scala 48:19]
    node _io_outs_T_6_bits_header = io_in_bits_header @[Decoupled.scala 48:19 Decoupled.scala 48:19]
    node _GEN_24 = validif(eq(UInt<1>("h0"), _T_6), _io_outs_T_6_bits_header) @[Decoupled.scala 48:19 Decoupled.scala 48:19]
    node _GEN_25 = validif(eq(UInt<1>("h1"), _T_6), _io_outs_T_6_bits_header) @[Decoupled.scala 48:19 Decoupled.scala 48:19]
    node _GEN_26 = validif(eq(UInt<2>("h2"), _T_6), _io_outs_T_6_bits_header) @[Decoupled.scala 48:19 Decoupled.scala 48:19]
    node _GEN_27 = validif(eq(UInt<2>("h3"), _T_6), _io_outs_T_6_bits_header) @[Decoupled.scala 48:19 Decoupled.scala 48:19]
    node _T_7 = bits(io_in_bits_header, 3, 0) @[Router.scala 81:108]
    node _T_8 = bits(reset, 0, 0) @[Router.scala 81:13]
    node _T_9 = eq(_T_8, UInt<1>("h0")) @[Router.scala 81:13]
    node _io_outs_T_5_ready = _GEN_12 @[Router.scala 78:30]
    node _GEN_28 = mux(_io_outs_T_5_ready, UInt<1>("h1"), UInt<1>("h0")) @[Router.scala 78:30 Decoupled.scala 65:20 Decoupled.scala 72:20]
    node _GEN_29 = mux(_io_outs_T_5_ready, _GEN_16, UInt<1>("h0")) @[Router.scala 78:30 Decoupled.scala 56:20]
    node _GEN_30 = mux(_io_outs_T_5_ready, _GEN_17, UInt<1>("h0")) @[Router.scala 78:30 Decoupled.scala 56:20]
    node _GEN_31 = mux(_io_outs_T_5_ready, _GEN_18, UInt<1>("h0")) @[Router.scala 78:30 Decoupled.scala 56:20]
    node _GEN_32 = mux(_io_outs_T_5_ready, _GEN_19, UInt<1>("h0")) @[Router.scala 78:30 Decoupled.scala 56:20]
    node _GEN_33 = validif(_io_outs_T_5_ready, _GEN_20) @[Router.scala 78:30]
    node _GEN_34 = validif(_io_outs_T_5_ready, _GEN_21) @[Router.scala 78:30]
    node _GEN_35 = validif(_io_outs_T_5_ready, _GEN_22) @[Router.scala 78:30]
    node _GEN_36 = validif(_io_outs_T_5_ready, _GEN_23) @[Router.scala 78:30]
    node _GEN_37 = validif(_io_outs_T_5_ready, _GEN_24) @[Router.scala 78:30]
    node _GEN_38 = validif(_io_outs_T_5_ready, _GEN_25) @[Router.scala 78:30]
    node _GEN_39 = validif(_io_outs_T_5_ready, _GEN_26) @[Router.scala 78:30]
    node _GEN_40 = validif(_io_outs_T_5_ready, _GEN_27) @[Router.scala 78:30]
    node _GEN_41 = validif(_io_outs_T_5_ready, _T_7) @[Router.scala 78:30 Router.scala 81:108]
    node _GEN_42 = validif(_io_outs_T_5_ready, clock) @[Router.scala 78:30 Router.scala 81:108]
    node _GEN_43 = validif(io_in_valid, _idx_T_1) @[Router.scala 75:26 Router.scala 77:18]
    node _GEN_44 = validif(io_in_valid, clock) @[Router.scala 75:26 Router.scala 77:18]
    node _GEN_45 = mux(io_in_valid, UInt<1>("h1"), UInt<1>("h0")) @[Router.scala 75:26 Router.scala 77:18 Router.scala 51:18]
    node _GEN_46 = mux(io_in_valid, _GEN_28, UInt<1>("h0")) @[Router.scala 75:26 Decoupled.scala 72:20]
    node _GEN_47 = mux(io_in_valid, _GEN_29, UInt<1>("h0")) @[Router.scala 75:26 Decoupled.scala 56:20]
    node _GEN_48 = mux(io_in_valid, _GEN_30, UInt<1>("h0")) @[Router.scala 75:26 Decoupled.scala 56:20]
    node _GEN_49 = mux(io_in_valid, _GEN_31, UInt<1>("h0")) @[Router.scala 75:26 Decoupled.scala 56:20]
    node _GEN_50 = mux(io_in_valid, _GEN_32, UInt<1>("h0")) @[Router.scala 75:26 Decoupled.scala 56:20]
    node _GEN_51 = validif(io_in_valid, _GEN_33) @[Router.scala 75:26]
    node _GEN_52 = validif(io_in_valid, _GEN_34) @[Router.scala 75:26]
    node _GEN_53 = validif(io_in_valid, _GEN_35) @[Router.scala 75:26]
    node _GEN_54 = validif(io_in_valid, _GEN_36) @[Router.scala 75:26]
    node _GEN_55 = validif(io_in_valid, _GEN_37) @[Router.scala 75:26]
    node _GEN_56 = validif(io_in_valid, _GEN_38) @[Router.scala 75:26]
    node _GEN_57 = validif(io_in_valid, _GEN_39) @[Router.scala 75:26]
    node _GEN_58 = validif(io_in_valid, _GEN_40) @[Router.scala 75:26]
    node _GEN_59 = validif(io_in_valid, _GEN_41) @[Router.scala 75:26]
    node _GEN_60 = validif(io_in_valid, _GEN_42) @[Router.scala 75:26]
    node _GEN_61 = mux(io_load_routing_table_request_valid, UInt<1>("h1"), UInt<1>("h0")) @[Router.scala 70:50 Decoupled.scala 65:20 Decoupled.scala 72:20]
    node _GEN_62 = validif(io_load_routing_table_request_valid, _T_2) @[Router.scala 70:50 Router.scala 72:8]
    node _GEN_63 = validif(io_load_routing_table_request_valid, clock) @[Router.scala 70:50 Router.scala 72:8]
    node _GEN_64 = validif(io_load_routing_table_request_valid, UInt<1>("h1")) @[Router.scala 70:50 Router.scala 72:19]
    node _GEN_65 = validif(io_load_routing_table_request_valid, io_load_routing_table_request_bits_data) @[Router.scala 70:50 Router.scala 72:19]
    node _GEN_66 = validif(eq(io_load_routing_table_request_valid, UInt<1>("h0")), _GEN_43) @[Router.scala 70:50]
    node _GEN_67 = validif(eq(io_load_routing_table_request_valid, UInt<1>("h0")), _GEN_44) @[Router.scala 70:50]
    node _GEN_68 = mux(io_load_routing_table_request_valid, UInt<1>("h0"), _GEN_45) @[Router.scala 70:50 Router.scala 51:18]
    node _GEN_69 = mux(io_load_routing_table_request_valid, UInt<1>("h0"), _GEN_46) @[Router.scala 70:50 Decoupled.scala 72:20]
    node _GEN_70 = mux(io_load_routing_table_request_valid, UInt<1>("h0"), _GEN_47) @[Router.scala 70:50 Decoupled.scala 56:20]
    node _GEN_71 = mux(io_load_routing_table_request_valid, UInt<1>("h0"), _GEN_48) @[Router.scala 70:50 Decoupled.scala 56:20]
    node _GEN_72 = mux(io_load_routing_table_request_valid, UInt<1>("h0"), _GEN_49) @[Router.scala 70:50 Decoupled.scala 56:20]
    node _GEN_73 = mux(io_load_routing_table_request_valid, UInt<1>("h0"), _GEN_50) @[Router.scala 70:50 Decoupled.scala 56:20]
    node _GEN_74 = validif(eq(io_load_routing_table_request_valid, UInt<1>("h0")), _GEN_51) @[Router.scala 70:50]
    node _GEN_75 = validif(eq(io_load_routing_table_request_valid, UInt<1>("h0")), _GEN_52) @[Router.scala 70:50]
    node _GEN_76 = validif(eq(io_load_routing_table_request_valid, UInt<1>("h0")), _GEN_53) @[Router.scala 70:50]
    node _GEN_77 = validif(eq(io_load_routing_table_request_valid, UInt<1>("h0")), _GEN_54) @[Router.scala 70:50]
    node _GEN_78 = validif(eq(io_load_routing_table_request_valid, UInt<1>("h0")), _GEN_55) @[Router.scala 70:50]
    node _GEN_79 = validif(eq(io_load_routing_table_request_valid, UInt<1>("h0")), _GEN_56) @[Router.scala 70:50]
    node _GEN_80 = validif(eq(io_load_routing_table_request_valid, UInt<1>("h0")), _GEN_57) @[Router.scala 70:50]
    node _GEN_81 = validif(eq(io_load_routing_table_request_valid, UInt<1>("h0")), _GEN_58) @[Router.scala 70:50]
    node _GEN_82 = validif(eq(io_load_routing_table_request_valid, UInt<1>("h0")), _GEN_59) @[Router.scala 70:50]
    node _GEN_83 = validif(eq(io_load_routing_table_request_valid, UInt<1>("h0")), _GEN_60) @[Router.scala 70:50]
    node _GEN_84 = mux(_T, UInt<1>("h1"), UInt<1>("h0")) @[Router.scala 65:85 Decoupled.scala 65:20 Decoupled.scala 72:20]
    node _GEN_85 = validif(_T, _T_1) @[Router.scala 65:85 Router.scala 66:43]
    node _GEN_86 = validif(_T, clock) @[Router.scala 65:85 Router.scala 66:43]
    node _GEN_87 = mux(_T, tbl.MPORT.data, UInt<1>("h0")) @[Router.scala 65:85 Decoupled.scala 48:19 Router.scala 57:39]
    node _GEN_88 = mux(_T, UInt<1>("h0"), _GEN_61) @[Router.scala 65:85 Decoupled.scala 72:20]
    node _GEN_89 = validif(eq(_T, UInt<1>("h0")), _GEN_62) @[Router.scala 65:85]
    node _GEN_90 = validif(eq(_T, UInt<1>("h0")), _GEN_63) @[Router.scala 65:85]
    node _GEN_91 = validif(eq(_T, UInt<1>("h0")), _GEN_64) @[Router.scala 65:85]
    node _GEN_92 = validif(eq(_T, UInt<1>("h0")), _GEN_65) @[Router.scala 65:85]
    node _GEN_93 = validif(eq(_T, UInt<1>("h0")), _GEN_66) @[Router.scala 65:85]
    node _GEN_94 = validif(eq(_T, UInt<1>("h0")), _GEN_67) @[Router.scala 65:85]
    node _GEN_95 = mux(_T, UInt<1>("h0"), _GEN_68) @[Router.scala 65:85 Router.scala 51:18]
    node _GEN_96 = mux(_T, UInt<1>("h0"), _GEN_69) @[Router.scala 65:85 Decoupled.scala 72:20]
    node _GEN_97 = mux(_T, UInt<1>("h0"), _GEN_70) @[Router.scala 65:85 Decoupled.scala 56:20]
    node _GEN_98 = mux(_T, UInt<1>("h0"), _GEN_71) @[Router.scala 65:85 Decoupled.scala 56:20]
    node _GEN_99 = mux(_T, UInt<1>("h0"), _GEN_72) @[Router.scala 65:85 Decoupled.scala 56:20]
    node _GEN_100 = mux(_T, UInt<1>("h0"), _GEN_73) @[Router.scala 65:85 Decoupled.scala 56:20]
    node _GEN_101 = validif(eq(_T, UInt<1>("h0")), _GEN_74) @[Router.scala 65:85]
    node _GEN_102 = validif(eq(_T, UInt<1>("h0")), _GEN_75) @[Router.scala 65:85]
    node _GEN_103 = validif(eq(_T, UInt<1>("h0")), _GEN_76) @[Router.scala 65:85]
    node _GEN_104 = validif(eq(_T, UInt<1>("h0")), _GEN_77) @[Router.scala 65:85]
    node _GEN_105 = validif(eq(_T, UInt<1>("h0")), _GEN_78) @[Router.scala 65:85]
    node _GEN_106 = validif(eq(_T, UInt<1>("h0")), _GEN_79) @[Router.scala 65:85]
    node _GEN_107 = validif(eq(_T, UInt<1>("h0")), _GEN_80) @[Router.scala 65:85]
    node _GEN_108 = validif(eq(_T, UInt<1>("h0")), _GEN_81) @[Router.scala 65:85]
    node _GEN_109 = validif(eq(_T, UInt<1>("h0")), _GEN_82) @[Router.scala 65:85]
    node _GEN_110 = validif(eq(_T, UInt<1>("h0")), _GEN_83) @[Router.scala 65:85]
    node _io_outs_0_bits_WIRE_header = UInt<8>("h0") @[Router.scala 60:29 Router.scala 60:29]
    node _io_outs_0_bits_WIRE_body = UInt<64>("h0") @[Router.scala 60:29 Router.scala 60:29]
    node _io_outs_1_bits_WIRE_header = UInt<8>("h0") @[Router.scala 60:29 Router.scala 60:29]
    node _io_outs_1_bits_WIRE_body = UInt<64>("h0") @[Router.scala 60:29 Router.scala 60:29]
    node _io_outs_2_bits_WIRE_header = UInt<8>("h0") @[Router.scala 60:29 Router.scala 60:29]
    node _io_outs_2_bits_WIRE_body = UInt<64>("h0") @[Router.scala 60:29 Router.scala 60:29]
    node _io_outs_3_bits_WIRE_header = UInt<8>("h0") @[Router.scala 60:29 Router.scala 60:29]
    node _io_outs_3_bits_WIRE_body = UInt<64>("h0") @[Router.scala 60:29 Router.scala 60:29]
    node _io_outs_T_5_valid = _GEN_13 @[Router.scala 78:30]
    node _io_outs_T_5_bits_header = _GEN_14 @[Router.scala 78:30]
    node _io_outs_T_5_bits_body = _GEN_15 @[Router.scala 78:30]
    io_read_routing_table_request_ready <= _GEN_84
    io_read_routing_table_response_valid <= _GEN_84
    io_read_routing_table_response_bits <= _GEN_87
    io_load_routing_table_request_ready <= _GEN_88
    io_in_ready <= _GEN_96
    io_outs_0_valid <= _GEN_97
    io_outs_0_bits_header <= _GEN_105
    io_outs_0_bits_body <= _GEN_101
    io_outs_1_valid <= _GEN_98
    io_outs_1_bits_header <= _GEN_106
    io_outs_1_bits_body <= _GEN_102
    io_outs_2_valid <= _GEN_99
    io_outs_2_bits_header <= _GEN_107
    io_outs_2_bits_body <= _GEN_103
    io_outs_3_valid <= _GEN_100
    io_outs_3_bits_header <= _GEN_108
    io_outs_3_bits_body <= _GEN_104
    tbl.MPORT.addr <= _GEN_85
    tbl.MPORT.en <= _GEN_84
    tbl.MPORT.clk <= _GEN_86
    tbl.idx.addr <= _GEN_93
    tbl.idx.en <= _GEN_95
    tbl.idx.clk <= _GEN_94
    tbl.MPORT_2.addr <= _GEN_109
    tbl.MPORT_2.en <= _GEN_96
    tbl.MPORT_2.clk <= _GEN_110
    tbl.MPORT_1.addr <= _GEN_89
    tbl.MPORT_1.en <= _GEN_88
    tbl.MPORT_1.clk <= _GEN_90
    tbl.MPORT_1.data <= bits(_GEN_92, 2, 0)
    tbl.MPORT_1.mask <= _GEN_91
    printf(clock, and(and(and(and(UInt<1>("h1"), eq(_T, UInt<1>("h0"))), io_load_routing_table_request_valid), _T_4), UInt<1>("h1")), "setting tbl(%d) to %d\n", io_load_routing_table_request_bits_addr, io_load_routing_table_request_bits_data) @[Router.scala 73:11]
    printf(clock, and(and(and(and(and(and(UInt<1>("h1"), eq(_T, UInt<1>("h0"))), eq(io_load_routing_table_request_valid, UInt<1>("h0"))), io_in_valid), _io_outs_T_5_ready), _T_9), UInt<1>("h1")), "got packet to route header %d, data %d, being routed to out(%d)\n", io_in_bits_header, io_in_bits_body, tbl.MPORT_2.data) @[Router.scala 81:13]