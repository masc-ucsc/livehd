circuit BankedStore :
  module BankedStore : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip sinkC_adr : {flip ready : UInt<1>, valid : UInt<1>, bits : {noop : UInt<1>, way : UInt<3>, set : UInt<10>, beat : UInt<3>, mask : UInt<1>}}, flip sinkC_dat : {data : UInt<64>}, flip sinkD_adr : {flip ready : UInt<1>, valid : UInt<1>, bits : {noop : UInt<1>, way : UInt<3>, set : UInt<10>, beat : UInt<3>, mask : UInt<1>}}, flip sinkD_dat : {data : UInt<64>}, flip sourceC_adr : {flip ready : UInt<1>, valid : UInt<1>, bits : {noop : UInt<1>, way : UInt<3>, set : UInt<10>, beat : UInt<3>, mask : UInt<1>}}, sourceC_dat : {data : UInt<64>}, flip sourceD_radr : {flip ready : UInt<1>, valid : UInt<1>, bits : {noop : UInt<1>, way : UInt<3>, set : UInt<10>, beat : UInt<3>, mask : UInt<1>}}, sourceD_rdat : {data : UInt<64>}, flip sourceD_wadr : {flip ready : UInt<1>, valid : UInt<1>, bits : {noop : UInt<1>, way : UInt<3>, set : UInt<10>, beat : UInt<3>, mask : UInt<1>}}, flip sourceD_wdat : {data : UInt<64>}}
    
    clock is invalid
    reset is invalid
    io is invalid
    smem cc_banks_0 : UInt<64>[16384], undefined @[DescribedSRAM.scala 23:26]
    smem cc_banks_1 : UInt<64>[16384], undefined @[DescribedSRAM.scala 23:26]
    smem cc_banks_2 : UInt<64>[16384], undefined @[DescribedSRAM.scala 23:26]
    smem cc_banks_3 : UInt<64>[16384], undefined @[DescribedSRAM.scala 23:26]
    node _T = bits(io.sinkC_dat.data, 63, 0) @[BankedStore.scala 122:19]
    node _T_1 = cat(io.sinkC_adr.bits.way, io.sinkC_adr.bits.set) @[Cat.scala 29:58]
    node _T_2 = cat(_T_1, io.sinkC_adr.bits.beat) @[Cat.scala 29:58]
    wire reqs_0 : {wen : UInt<1>, index : UInt<14>, bankSel : UInt<4>, bankSum : UInt<4>, bankEn : UInt<4>, data : UInt<64>[4]} @[BankedStore.scala 127:19]
    reqs_0 is invalid @[BankedStore.scala 127:19]
    node _T_3 = bits(_T_2, 1, 0) @[BankedStore.scala 129:28]
    node _T_4 = bits(_T_3, 1, 0) @[OneHot.scala 64:49]
    node _T_5 = dshl(UInt<1>("h01"), _T_4) @[OneHot.scala 65:12]
    node _T_6 = bits(_T_5, 3, 0) @[OneHot.scala 65:27]
    node _T_7 = bits(reqs_0.bankSum, 0, 0) @[BankedStore.scala 130:71]
    node _T_8 = and(_T_7, io.sinkC_adr.bits.mask) @[BankedStore.scala 130:96]
    node _T_9 = orr(_T_8) @[BankedStore.scala 130:101]
    node _T_10 = eq(_T_9, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_11 = bits(reqs_0.bankSum, 1, 1) @[BankedStore.scala 130:71]
    node _T_12 = and(_T_11, io.sinkC_adr.bits.mask) @[BankedStore.scala 130:96]
    node _T_13 = orr(_T_12) @[BankedStore.scala 130:101]
    node _T_14 = eq(_T_13, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_15 = bits(reqs_0.bankSum, 2, 2) @[BankedStore.scala 130:71]
    node _T_16 = and(_T_15, io.sinkC_adr.bits.mask) @[BankedStore.scala 130:96]
    node _T_17 = orr(_T_16) @[BankedStore.scala 130:101]
    node _T_18 = eq(_T_17, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_19 = bits(reqs_0.bankSum, 3, 3) @[BankedStore.scala 130:71]
    node _T_20 = and(_T_19, io.sinkC_adr.bits.mask) @[BankedStore.scala 130:96]
    node _T_21 = orr(_T_20) @[BankedStore.scala 130:101]
    node _T_22 = eq(_T_21, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_23 = cat(_T_14, _T_10) @[Cat.scala 29:58]
    node _T_24 = cat(_T_22, _T_18) @[Cat.scala 29:58]
    node _T_25 = cat(_T_24, _T_23) @[Cat.scala 29:58]
    node _T_26 = bits(_T_2, 1, 0) @[BankedStore.scala 131:23]
    node _T_27 = dshr(_T_25, _T_26) @[BankedStore.scala 131:21]
    node _T_28 = bits(_T_27, 0, 0) @[BankedStore.scala 131:21]
    io.sinkC_adr.ready <= _T_28 @[BankedStore.scala 131:13]
    reqs_0.wen <= UInt<1>("h01") @[BankedStore.scala 133:18]
    node _T_29 = shr(_T_2, 2) @[BankedStore.scala 134:23]
    reqs_0.index <= _T_29 @[BankedStore.scala 134:18]
    node _T_30 = bits(_T_6, 0, 0) @[Bitwise.scala 26:51]
    node _T_31 = bits(_T_6, 1, 1) @[Bitwise.scala 26:51]
    node _T_32 = bits(_T_6, 2, 2) @[Bitwise.scala 26:51]
    node _T_33 = bits(_T_6, 3, 3) @[Bitwise.scala 26:51]
    node _T_34 = cat(_T_31, _T_30) @[Cat.scala 29:58]
    node _T_35 = cat(_T_33, _T_32) @[Cat.scala 29:58]
    node _T_36 = cat(_T_35, _T_34) @[Cat.scala 29:58]
    node _T_37 = bits(io.sinkC_adr.bits.mask, 0, 0) @[Bitwise.scala 72:15]
    node _T_38 = mux(_T_37, UInt<4>("h0f"), UInt<4>("h00")) @[Bitwise.scala 72:12]
    node _T_39 = and(_T_36, _T_38) @[BankedStore.scala 135:65]
    node _T_40 = mux(io.sinkC_adr.valid, _T_39, UInt<1>("h00")) @[BankedStore.scala 135:24]
    reqs_0.bankSel <= _T_40 @[BankedStore.scala 135:18]
    node _T_41 = bits(_T_25, 0, 0) @[Bitwise.scala 26:51]
    node _T_42 = bits(_T_25, 1, 1) @[Bitwise.scala 26:51]
    node _T_43 = bits(_T_25, 2, 2) @[Bitwise.scala 26:51]
    node _T_44 = bits(_T_25, 3, 3) @[Bitwise.scala 26:51]
    node _T_45 = cat(_T_42, _T_41) @[Cat.scala 29:58]
    node _T_46 = cat(_T_44, _T_43) @[Cat.scala 29:58]
    node _T_47 = cat(_T_46, _T_45) @[Cat.scala 29:58]
    node _T_48 = and(reqs_0.bankSel, _T_47) @[BankedStore.scala 136:59]
    node _T_49 = mux(io.sinkC_adr.bits.noop, UInt<1>("h00"), _T_48) @[BankedStore.scala 136:24]
    reqs_0.bankEn <= _T_49 @[BankedStore.scala 136:18]
    wire _T_50 : UInt<64>[4] @[BankedStore.scala 137:24]
    _T_50 is invalid @[BankedStore.scala 137:24]
    _T_50[0] <= _T @[BankedStore.scala 137:24]
    _T_50[1] <= _T @[BankedStore.scala 137:24]
    _T_50[2] <= _T @[BankedStore.scala 137:24]
    _T_50[3] <= _T @[BankedStore.scala 137:24]
    reqs_0.data <- _T_50 @[BankedStore.scala 137:18]
    node _T_51 = bits(io.sinkD_dat.data, 63, 0) @[BankedStore.scala 122:19]
    node _T_52 = cat(io.sinkD_adr.bits.way, io.sinkD_adr.bits.set) @[Cat.scala 29:58]
    node _T_53 = cat(_T_52, io.sinkD_adr.bits.beat) @[Cat.scala 29:58]
    wire reqs_2 : {wen : UInt<1>, index : UInt<14>, bankSel : UInt<4>, bankSum : UInt<4>, bankEn : UInt<4>, data : UInt<64>[4]} @[BankedStore.scala 127:19]
    reqs_2 is invalid @[BankedStore.scala 127:19]
    node _T_54 = bits(_T_53, 1, 0) @[BankedStore.scala 129:28]
    node _T_55 = bits(_T_54, 1, 0) @[OneHot.scala 64:49]
    node _T_56 = dshl(UInt<1>("h01"), _T_55) @[OneHot.scala 65:12]
    node _T_57 = bits(_T_56, 3, 0) @[OneHot.scala 65:27]
    node _T_58 = bits(reqs_2.bankSum, 0, 0) @[BankedStore.scala 130:71]
    node _T_59 = and(_T_58, io.sinkD_adr.bits.mask) @[BankedStore.scala 130:96]
    node _T_60 = orr(_T_59) @[BankedStore.scala 130:101]
    node _T_61 = eq(_T_60, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_62 = bits(reqs_2.bankSum, 1, 1) @[BankedStore.scala 130:71]
    node _T_63 = and(_T_62, io.sinkD_adr.bits.mask) @[BankedStore.scala 130:96]
    node _T_64 = orr(_T_63) @[BankedStore.scala 130:101]
    node _T_65 = eq(_T_64, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_66 = bits(reqs_2.bankSum, 2, 2) @[BankedStore.scala 130:71]
    node _T_67 = and(_T_66, io.sinkD_adr.bits.mask) @[BankedStore.scala 130:96]
    node _T_68 = orr(_T_67) @[BankedStore.scala 130:101]
    node _T_69 = eq(_T_68, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_70 = bits(reqs_2.bankSum, 3, 3) @[BankedStore.scala 130:71]
    node _T_71 = and(_T_70, io.sinkD_adr.bits.mask) @[BankedStore.scala 130:96]
    node _T_72 = orr(_T_71) @[BankedStore.scala 130:101]
    node _T_73 = eq(_T_72, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_74 = cat(_T_65, _T_61) @[Cat.scala 29:58]
    node _T_75 = cat(_T_73, _T_69) @[Cat.scala 29:58]
    node _T_76 = cat(_T_75, _T_74) @[Cat.scala 29:58]
    node _T_77 = bits(_T_53, 1, 0) @[BankedStore.scala 131:23]
    node _T_78 = dshr(_T_76, _T_77) @[BankedStore.scala 131:21]
    node _T_79 = bits(_T_78, 0, 0) @[BankedStore.scala 131:21]
    io.sinkD_adr.ready <= _T_79 @[BankedStore.scala 131:13]
    reqs_2.wen <= UInt<1>("h01") @[BankedStore.scala 133:18]
    node _T_80 = shr(_T_53, 2) @[BankedStore.scala 134:23]
    reqs_2.index <= _T_80 @[BankedStore.scala 134:18]
    node _T_81 = bits(_T_57, 0, 0) @[Bitwise.scala 26:51]
    node _T_82 = bits(_T_57, 1, 1) @[Bitwise.scala 26:51]
    node _T_83 = bits(_T_57, 2, 2) @[Bitwise.scala 26:51]
    node _T_84 = bits(_T_57, 3, 3) @[Bitwise.scala 26:51]
    node _T_85 = cat(_T_82, _T_81) @[Cat.scala 29:58]
    node _T_86 = cat(_T_84, _T_83) @[Cat.scala 29:58]
    node _T_87 = cat(_T_86, _T_85) @[Cat.scala 29:58]
    node _T_88 = bits(io.sinkD_adr.bits.mask, 0, 0) @[Bitwise.scala 72:15]
    node _T_89 = mux(_T_88, UInt<4>("h0f"), UInt<4>("h00")) @[Bitwise.scala 72:12]
    node _T_90 = and(_T_87, _T_89) @[BankedStore.scala 135:65]
    node _T_91 = mux(io.sinkD_adr.valid, _T_90, UInt<1>("h00")) @[BankedStore.scala 135:24]
    reqs_2.bankSel <= _T_91 @[BankedStore.scala 135:18]
    node _T_92 = bits(_T_76, 0, 0) @[Bitwise.scala 26:51]
    node _T_93 = bits(_T_76, 1, 1) @[Bitwise.scala 26:51]
    node _T_94 = bits(_T_76, 2, 2) @[Bitwise.scala 26:51]
    node _T_95 = bits(_T_76, 3, 3) @[Bitwise.scala 26:51]
    node _T_96 = cat(_T_93, _T_92) @[Cat.scala 29:58]
    node _T_97 = cat(_T_95, _T_94) @[Cat.scala 29:58]
    node _T_98 = cat(_T_97, _T_96) @[Cat.scala 29:58]
    node _T_99 = and(reqs_2.bankSel, _T_98) @[BankedStore.scala 136:59]
    node _T_100 = mux(io.sinkD_adr.bits.noop, UInt<1>("h00"), _T_99) @[BankedStore.scala 136:24]
    reqs_2.bankEn <= _T_100 @[BankedStore.scala 136:18]
    wire _T_101 : UInt<64>[4] @[BankedStore.scala 137:24]
    _T_101 is invalid @[BankedStore.scala 137:24]
    _T_101[0] <= _T_51 @[BankedStore.scala 137:24]
    _T_101[1] <= _T_51 @[BankedStore.scala 137:24]
    _T_101[2] <= _T_51 @[BankedStore.scala 137:24]
    _T_101[3] <= _T_51 @[BankedStore.scala 137:24]
    reqs_2.data <- _T_101 @[BankedStore.scala 137:18]
    node _T_102 = cat(io.sourceC_adr.bits.way, io.sourceC_adr.bits.set) @[Cat.scala 29:58]
    node _T_103 = cat(_T_102, io.sourceC_adr.bits.beat) @[Cat.scala 29:58]
    wire reqs_1 : {wen : UInt<1>, index : UInt<14>, bankSel : UInt<4>, bankSum : UInt<4>, bankEn : UInt<4>, data : UInt<64>[4]} @[BankedStore.scala 127:19]
    reqs_1 is invalid @[BankedStore.scala 127:19]
    node _T_104 = bits(_T_103, 1, 0) @[BankedStore.scala 129:28]
    node _T_105 = bits(_T_104, 1, 0) @[OneHot.scala 64:49]
    node _T_106 = dshl(UInt<1>("h01"), _T_105) @[OneHot.scala 65:12]
    node _T_107 = bits(_T_106, 3, 0) @[OneHot.scala 65:27]
    node _T_108 = bits(reqs_1.bankSum, 0, 0) @[BankedStore.scala 130:71]
    node _T_109 = and(_T_108, io.sourceC_adr.bits.mask) @[BankedStore.scala 130:96]
    node _T_110 = orr(_T_109) @[BankedStore.scala 130:101]
    node _T_111 = eq(_T_110, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_112 = bits(reqs_1.bankSum, 1, 1) @[BankedStore.scala 130:71]
    node _T_113 = and(_T_112, io.sourceC_adr.bits.mask) @[BankedStore.scala 130:96]
    node _T_114 = orr(_T_113) @[BankedStore.scala 130:101]
    node _T_115 = eq(_T_114, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_116 = bits(reqs_1.bankSum, 2, 2) @[BankedStore.scala 130:71]
    node _T_117 = and(_T_116, io.sourceC_adr.bits.mask) @[BankedStore.scala 130:96]
    node _T_118 = orr(_T_117) @[BankedStore.scala 130:101]
    node _T_119 = eq(_T_118, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_120 = bits(reqs_1.bankSum, 3, 3) @[BankedStore.scala 130:71]
    node _T_121 = and(_T_120, io.sourceC_adr.bits.mask) @[BankedStore.scala 130:96]
    node _T_122 = orr(_T_121) @[BankedStore.scala 130:101]
    node _T_123 = eq(_T_122, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_124 = cat(_T_115, _T_111) @[Cat.scala 29:58]
    node _T_125 = cat(_T_123, _T_119) @[Cat.scala 29:58]
    node _T_126 = cat(_T_125, _T_124) @[Cat.scala 29:58]
    node _T_127 = bits(_T_103, 1, 0) @[BankedStore.scala 131:23]
    node _T_128 = dshr(_T_126, _T_127) @[BankedStore.scala 131:21]
    node _T_129 = bits(_T_128, 0, 0) @[BankedStore.scala 131:21]
    io.sourceC_adr.ready <= _T_129 @[BankedStore.scala 131:13]
    reqs_1.wen <= UInt<1>("h00") @[BankedStore.scala 133:18]
    node _T_130 = shr(_T_103, 2) @[BankedStore.scala 134:23]
    reqs_1.index <= _T_130 @[BankedStore.scala 134:18]
    node _T_131 = bits(_T_107, 0, 0) @[Bitwise.scala 26:51]
    node _T_132 = bits(_T_107, 1, 1) @[Bitwise.scala 26:51]
    node _T_133 = bits(_T_107, 2, 2) @[Bitwise.scala 26:51]
    node _T_134 = bits(_T_107, 3, 3) @[Bitwise.scala 26:51]
    node _T_135 = cat(_T_132, _T_131) @[Cat.scala 29:58]
    node _T_136 = cat(_T_134, _T_133) @[Cat.scala 29:58]
    node _T_137 = cat(_T_136, _T_135) @[Cat.scala 29:58]
    node _T_138 = bits(io.sourceC_adr.bits.mask, 0, 0) @[Bitwise.scala 72:15]
    node _T_139 = mux(_T_138, UInt<4>("h0f"), UInt<4>("h00")) @[Bitwise.scala 72:12]
    node _T_140 = and(_T_137, _T_139) @[BankedStore.scala 135:65]
    node _T_141 = mux(io.sourceC_adr.valid, _T_140, UInt<1>("h00")) @[BankedStore.scala 135:24]
    reqs_1.bankSel <= _T_141 @[BankedStore.scala 135:18]
    node _T_142 = bits(_T_126, 0, 0) @[Bitwise.scala 26:51]
    node _T_143 = bits(_T_126, 1, 1) @[Bitwise.scala 26:51]
    node _T_144 = bits(_T_126, 2, 2) @[Bitwise.scala 26:51]
    node _T_145 = bits(_T_126, 3, 3) @[Bitwise.scala 26:51]
    node _T_146 = cat(_T_143, _T_142) @[Cat.scala 29:58]
    node _T_147 = cat(_T_145, _T_144) @[Cat.scala 29:58]
    node _T_148 = cat(_T_147, _T_146) @[Cat.scala 29:58]
    node _T_149 = and(reqs_1.bankSel, _T_148) @[BankedStore.scala 136:59]
    node _T_150 = mux(io.sourceC_adr.bits.noop, UInt<1>("h00"), _T_149) @[BankedStore.scala 136:24]
    reqs_1.bankEn <= _T_150 @[BankedStore.scala 136:18]
    wire _T_151 : UInt<64>[4] @[BankedStore.scala 137:24]
    _T_151 is invalid @[BankedStore.scala 137:24]
    _T_151[0] <= UInt<64>("h00") @[BankedStore.scala 137:24]
    _T_151[1] <= UInt<64>("h00") @[BankedStore.scala 137:24]
    _T_151[2] <= UInt<64>("h00") @[BankedStore.scala 137:24]
    _T_151[3] <= UInt<64>("h00") @[BankedStore.scala 137:24]
    reqs_1.data <- _T_151 @[BankedStore.scala 137:18]
    node _T_152 = cat(io.sourceD_radr.bits.way, io.sourceD_radr.bits.set) @[Cat.scala 29:58]
    node _T_153 = cat(_T_152, io.sourceD_radr.bits.beat) @[Cat.scala 29:58]
    wire reqs_4 : {wen : UInt<1>, index : UInt<14>, bankSel : UInt<4>, bankSum : UInt<4>, bankEn : UInt<4>, data : UInt<64>[4]} @[BankedStore.scala 127:19]
    reqs_4 is invalid @[BankedStore.scala 127:19]
    node _T_154 = bits(_T_153, 1, 0) @[BankedStore.scala 129:28]
    node _T_155 = bits(_T_154, 1, 0) @[OneHot.scala 64:49]
    node _T_156 = dshl(UInt<1>("h01"), _T_155) @[OneHot.scala 65:12]
    node _T_157 = bits(_T_156, 3, 0) @[OneHot.scala 65:27]
    node _T_158 = bits(reqs_4.bankSum, 0, 0) @[BankedStore.scala 130:71]
    node _T_159 = and(_T_158, io.sourceD_radr.bits.mask) @[BankedStore.scala 130:96]
    node _T_160 = orr(_T_159) @[BankedStore.scala 130:101]
    node _T_161 = eq(_T_160, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_162 = bits(reqs_4.bankSum, 1, 1) @[BankedStore.scala 130:71]
    node _T_163 = and(_T_162, io.sourceD_radr.bits.mask) @[BankedStore.scala 130:96]
    node _T_164 = orr(_T_163) @[BankedStore.scala 130:101]
    node _T_165 = eq(_T_164, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_166 = bits(reqs_4.bankSum, 2, 2) @[BankedStore.scala 130:71]
    node _T_167 = and(_T_166, io.sourceD_radr.bits.mask) @[BankedStore.scala 130:96]
    node _T_168 = orr(_T_167) @[BankedStore.scala 130:101]
    node _T_169 = eq(_T_168, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_170 = bits(reqs_4.bankSum, 3, 3) @[BankedStore.scala 130:71]
    node _T_171 = and(_T_170, io.sourceD_radr.bits.mask) @[BankedStore.scala 130:96]
    node _T_172 = orr(_T_171) @[BankedStore.scala 130:101]
    node _T_173 = eq(_T_172, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_174 = cat(_T_165, _T_161) @[Cat.scala 29:58]
    node _T_175 = cat(_T_173, _T_169) @[Cat.scala 29:58]
    node _T_176 = cat(_T_175, _T_174) @[Cat.scala 29:58]
    node _T_177 = bits(_T_153, 1, 0) @[BankedStore.scala 131:23]
    node _T_178 = dshr(_T_176, _T_177) @[BankedStore.scala 131:21]
    node _T_179 = bits(_T_178, 0, 0) @[BankedStore.scala 131:21]
    io.sourceD_radr.ready <= _T_179 @[BankedStore.scala 131:13]
    reqs_4.wen <= UInt<1>("h00") @[BankedStore.scala 133:18]
    node _T_180 = shr(_T_153, 2) @[BankedStore.scala 134:23]
    reqs_4.index <= _T_180 @[BankedStore.scala 134:18]
    node _T_181 = bits(_T_157, 0, 0) @[Bitwise.scala 26:51]
    node _T_182 = bits(_T_157, 1, 1) @[Bitwise.scala 26:51]
    node _T_183 = bits(_T_157, 2, 2) @[Bitwise.scala 26:51]
    node _T_184 = bits(_T_157, 3, 3) @[Bitwise.scala 26:51]
    node _T_185 = cat(_T_182, _T_181) @[Cat.scala 29:58]
    node _T_186 = cat(_T_184, _T_183) @[Cat.scala 29:58]
    node _T_187 = cat(_T_186, _T_185) @[Cat.scala 29:58]
    node _T_188 = bits(io.sourceD_radr.bits.mask, 0, 0) @[Bitwise.scala 72:15]
    node _T_189 = mux(_T_188, UInt<4>("h0f"), UInt<4>("h00")) @[Bitwise.scala 72:12]
    node _T_190 = and(_T_187, _T_189) @[BankedStore.scala 135:65]
    node _T_191 = mux(io.sourceD_radr.valid, _T_190, UInt<1>("h00")) @[BankedStore.scala 135:24]
    reqs_4.bankSel <= _T_191 @[BankedStore.scala 135:18]
    node _T_192 = bits(_T_176, 0, 0) @[Bitwise.scala 26:51]
    node _T_193 = bits(_T_176, 1, 1) @[Bitwise.scala 26:51]
    node _T_194 = bits(_T_176, 2, 2) @[Bitwise.scala 26:51]
    node _T_195 = bits(_T_176, 3, 3) @[Bitwise.scala 26:51]
    node _T_196 = cat(_T_193, _T_192) @[Cat.scala 29:58]
    node _T_197 = cat(_T_195, _T_194) @[Cat.scala 29:58]
    node _T_198 = cat(_T_197, _T_196) @[Cat.scala 29:58]
    node _T_199 = and(reqs_4.bankSel, _T_198) @[BankedStore.scala 136:59]
    node _T_200 = mux(io.sourceD_radr.bits.noop, UInt<1>("h00"), _T_199) @[BankedStore.scala 136:24]
    reqs_4.bankEn <= _T_200 @[BankedStore.scala 136:18]
    wire _T_201 : UInt<64>[4] @[BankedStore.scala 137:24]
    _T_201 is invalid @[BankedStore.scala 137:24]
    _T_201[0] <= UInt<64>("h00") @[BankedStore.scala 137:24]
    _T_201[1] <= UInt<64>("h00") @[BankedStore.scala 137:24]
    _T_201[2] <= UInt<64>("h00") @[BankedStore.scala 137:24]
    _T_201[3] <= UInt<64>("h00") @[BankedStore.scala 137:24]
    reqs_4.data <- _T_201 @[BankedStore.scala 137:18]
    node _T_202 = bits(io.sourceD_wdat.data, 63, 0) @[BankedStore.scala 122:19]
    node _T_203 = cat(io.sourceD_wadr.bits.way, io.sourceD_wadr.bits.set) @[Cat.scala 29:58]
    node _T_204 = cat(_T_203, io.sourceD_wadr.bits.beat) @[Cat.scala 29:58]
    wire reqs_3 : {wen : UInt<1>, index : UInt<14>, bankSel : UInt<4>, bankSum : UInt<4>, bankEn : UInt<4>, data : UInt<64>[4]} @[BankedStore.scala 127:19]
    reqs_3 is invalid @[BankedStore.scala 127:19]
    node _T_205 = bits(_T_204, 1, 0) @[BankedStore.scala 129:28]
    node _T_206 = bits(_T_205, 1, 0) @[OneHot.scala 64:49]
    node _T_207 = dshl(UInt<1>("h01"), _T_206) @[OneHot.scala 65:12]
    node _T_208 = bits(_T_207, 3, 0) @[OneHot.scala 65:27]
    node _T_209 = bits(reqs_3.bankSum, 0, 0) @[BankedStore.scala 130:71]
    node _T_210 = and(_T_209, io.sourceD_wadr.bits.mask) @[BankedStore.scala 130:96]
    node _T_211 = orr(_T_210) @[BankedStore.scala 130:101]
    node _T_212 = eq(_T_211, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_213 = bits(reqs_3.bankSum, 1, 1) @[BankedStore.scala 130:71]
    node _T_214 = and(_T_213, io.sourceD_wadr.bits.mask) @[BankedStore.scala 130:96]
    node _T_215 = orr(_T_214) @[BankedStore.scala 130:101]
    node _T_216 = eq(_T_215, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_217 = bits(reqs_3.bankSum, 2, 2) @[BankedStore.scala 130:71]
    node _T_218 = and(_T_217, io.sourceD_wadr.bits.mask) @[BankedStore.scala 130:96]
    node _T_219 = orr(_T_218) @[BankedStore.scala 130:101]
    node _T_220 = eq(_T_219, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_221 = bits(reqs_3.bankSum, 3, 3) @[BankedStore.scala 130:71]
    node _T_222 = and(_T_221, io.sourceD_wadr.bits.mask) @[BankedStore.scala 130:96]
    node _T_223 = orr(_T_222) @[BankedStore.scala 130:101]
    node _T_224 = eq(_T_223, UInt<1>("h00")) @[BankedStore.scala 130:58]
    node _T_225 = cat(_T_216, _T_212) @[Cat.scala 29:58]
    node _T_226 = cat(_T_224, _T_220) @[Cat.scala 29:58]
    node _T_227 = cat(_T_226, _T_225) @[Cat.scala 29:58]
    node _T_228 = bits(_T_204, 1, 0) @[BankedStore.scala 131:23]
    node _T_229 = dshr(_T_227, _T_228) @[BankedStore.scala 131:21]
    node _T_230 = bits(_T_229, 0, 0) @[BankedStore.scala 131:21]
    io.sourceD_wadr.ready <= _T_230 @[BankedStore.scala 131:13]
    reqs_3.wen <= UInt<1>("h01") @[BankedStore.scala 133:18]
    node _T_231 = shr(_T_204, 2) @[BankedStore.scala 134:23]
    reqs_3.index <= _T_231 @[BankedStore.scala 134:18]
    node _T_232 = bits(_T_208, 0, 0) @[Bitwise.scala 26:51]
    node _T_233 = bits(_T_208, 1, 1) @[Bitwise.scala 26:51]
    node _T_234 = bits(_T_208, 2, 2) @[Bitwise.scala 26:51]
    node _T_235 = bits(_T_208, 3, 3) @[Bitwise.scala 26:51]
    node _T_236 = cat(_T_233, _T_232) @[Cat.scala 29:58]
    node _T_237 = cat(_T_235, _T_234) @[Cat.scala 29:58]
    node _T_238 = cat(_T_237, _T_236) @[Cat.scala 29:58]
    node _T_239 = bits(io.sourceD_wadr.bits.mask, 0, 0) @[Bitwise.scala 72:15]
    node _T_240 = mux(_T_239, UInt<4>("h0f"), UInt<4>("h00")) @[Bitwise.scala 72:12]
    node _T_241 = and(_T_238, _T_240) @[BankedStore.scala 135:65]
    node _T_242 = mux(io.sourceD_wadr.valid, _T_241, UInt<1>("h00")) @[BankedStore.scala 135:24]
    reqs_3.bankSel <= _T_242 @[BankedStore.scala 135:18]
    node _T_243 = bits(_T_227, 0, 0) @[Bitwise.scala 26:51]
    node _T_244 = bits(_T_227, 1, 1) @[Bitwise.scala 26:51]
    node _T_245 = bits(_T_227, 2, 2) @[Bitwise.scala 26:51]
    node _T_246 = bits(_T_227, 3, 3) @[Bitwise.scala 26:51]
    node _T_247 = cat(_T_244, _T_243) @[Cat.scala 29:58]
    node _T_248 = cat(_T_246, _T_245) @[Cat.scala 29:58]
    node _T_249 = cat(_T_248, _T_247) @[Cat.scala 29:58]
    node _T_250 = and(reqs_3.bankSel, _T_249) @[BankedStore.scala 136:59]
    node _T_251 = mux(io.sourceD_wadr.bits.noop, UInt<1>("h00"), _T_250) @[BankedStore.scala 136:24]
    reqs_3.bankEn <= _T_251 @[BankedStore.scala 136:18]
    wire _T_252 : UInt<64>[4] @[BankedStore.scala 137:24]
    _T_252 is invalid @[BankedStore.scala 137:24]
    _T_252[0] <= _T_202 @[BankedStore.scala 137:24]
    _T_252[1] <= _T_202 @[BankedStore.scala 137:24]
    _T_252[2] <= _T_202 @[BankedStore.scala 137:24]
    _T_252[3] <= _T_202 @[BankedStore.scala 137:24]
    reqs_3.data <- _T_252 @[BankedStore.scala 137:18]
    reqs_0.bankSum <= UInt<1>("h00") @[BankedStore.scala 159:17]
    node _T_253 = or(reqs_0.bankSel, UInt<1>("h00")) @[BankedStore.scala 160:17]
    reqs_1.bankSum <= _T_253 @[BankedStore.scala 159:17]
    node _T_254 = or(reqs_1.bankSel, _T_253) @[BankedStore.scala 160:17]
    reqs_2.bankSum <= _T_254 @[BankedStore.scala 159:17]
    node _T_255 = or(reqs_2.bankSel, _T_254) @[BankedStore.scala 160:17]
    reqs_3.bankSum <= _T_255 @[BankedStore.scala 159:17]
    node _T_256 = or(reqs_3.bankSel, _T_255) @[BankedStore.scala 160:17]
    reqs_4.bankSum <= _T_256 @[BankedStore.scala 159:17]
    node _T_257 = or(reqs_4.bankSel, _T_256) @[BankedStore.scala 160:17]
    node _T_258 = bits(reqs_0.bankEn, 0, 0) @[BankedStore.scala 164:32]
    node _T_259 = bits(reqs_1.bankEn, 0, 0) @[BankedStore.scala 164:32]
    node _T_260 = bits(reqs_2.bankEn, 0, 0) @[BankedStore.scala 164:32]
    node _T_261 = bits(reqs_3.bankEn, 0, 0) @[BankedStore.scala 164:32]
    node _T_262 = bits(reqs_4.bankEn, 0, 0) @[BankedStore.scala 164:32]
    node _T_263 = or(_T_258, _T_259) @[BankedStore.scala 164:45]
    node _T_264 = or(_T_263, _T_260) @[BankedStore.scala 164:45]
    node _T_265 = or(_T_264, _T_261) @[BankedStore.scala 164:45]
    node _T_266 = or(_T_265, _T_262) @[BankedStore.scala 164:45]
    node _T_267 = bits(reqs_0.bankSel, 0, 0) @[BankedStore.scala 165:33]
    node _T_268 = bits(reqs_1.bankSel, 0, 0) @[BankedStore.scala 165:33]
    node _T_269 = bits(reqs_2.bankSel, 0, 0) @[BankedStore.scala 165:33]
    node _T_270 = bits(reqs_3.bankSel, 0, 0) @[BankedStore.scala 165:33]
    node _T_271 = bits(reqs_4.bankSel, 0, 0) @[BankedStore.scala 165:33]
    node _T_272 = mux(_T_270, reqs_3.wen, reqs_4.wen) @[Mux.scala 47:69]
    node _T_273 = mux(_T_269, reqs_2.wen, _T_272) @[Mux.scala 47:69]
    node _T_274 = mux(_T_268, reqs_1.wen, _T_273) @[Mux.scala 47:69]
    node _T_275 = mux(_T_267, reqs_0.wen, _T_274) @[Mux.scala 47:69]
    node _T_276 = mux(_T_270, reqs_3.index, reqs_4.index) @[Mux.scala 47:69]
    node _T_277 = mux(_T_269, reqs_2.index, _T_276) @[Mux.scala 47:69]
    node _T_278 = mux(_T_268, reqs_1.index, _T_277) @[Mux.scala 47:69]
    node _T_279 = mux(_T_267, reqs_0.index, _T_278) @[Mux.scala 47:69]
    node _T_280 = mux(_T_270, reqs_3.data[0], reqs_4.data[0]) @[Mux.scala 47:69]
    node _T_281 = mux(_T_269, reqs_2.data[0], _T_280) @[Mux.scala 47:69]
    node _T_282 = mux(_T_268, reqs_1.data[0], _T_281) @[Mux.scala 47:69]
    node _T_283 = mux(_T_267, reqs_0.data[0], _T_282) @[Mux.scala 47:69]
    node _T_284 = and(_T_275, _T_266) @[BankedStore.scala 170:15]
    when _T_284 : @[BankedStore.scala 170:22]
      write mport _T_285 = cc_banks_0[_T_279], clock
      _T_285 <= _T_283
      skip @[BankedStore.scala 170:22]
    node _T_286 = eq(_T_275, UInt<1>("h00")) @[BankedStore.scala 171:27]
    node _T_287 = and(_T_286, _T_266) @[BankedStore.scala 171:32]
    wire _T_288 : UInt @[BankedStore.scala 171:21]
    _T_288 is invalid @[BankedStore.scala 171:21]
    _T_288 is invalid @[BankedStore.scala 171:21]
    when _T_287 : @[BankedStore.scala 171:21]
      _T_288 <= _T_279 @[BankedStore.scala 171:21]
      node _T_289 = or(_T_288, UInt<14>("h00")) @[BankedStore.scala 171:21]
      node _T_290 = bits(_T_289, 13, 0) @[BankedStore.scala 171:21]
      read mport _T_291 = cc_banks_0[_T_290], clock @[BankedStore.scala 171:21]
      skip @[BankedStore.scala 171:21]
    node _T_292 = eq(_T_275, UInt<1>("h00")) @[BankedStore.scala 171:48]
    node _T_293 = and(_T_292, _T_266) @[BankedStore.scala 171:53]
    reg _T_294 : UInt<1>, clock @[BankedStore.scala 171:47]
    _T_294 <= _T_293 @[BankedStore.scala 171:47]
    reg _T_295 : UInt<64>, clock @[Reg.scala 15:16]
    when _T_294 : @[Reg.scala 16:19]
      _T_295 <= _T_291 @[Reg.scala 16:23]
      skip @[Reg.scala 16:19]
    node _T_296 = bits(reqs_0.bankEn, 1, 1) @[BankedStore.scala 164:32]
    node _T_297 = bits(reqs_1.bankEn, 1, 1) @[BankedStore.scala 164:32]
    node _T_298 = bits(reqs_2.bankEn, 1, 1) @[BankedStore.scala 164:32]
    node _T_299 = bits(reqs_3.bankEn, 1, 1) @[BankedStore.scala 164:32]
    node _T_300 = bits(reqs_4.bankEn, 1, 1) @[BankedStore.scala 164:32]
    node _T_301 = or(_T_296, _T_297) @[BankedStore.scala 164:45]
    node _T_302 = or(_T_301, _T_298) @[BankedStore.scala 164:45]
    node _T_303 = or(_T_302, _T_299) @[BankedStore.scala 164:45]
    node _T_304 = or(_T_303, _T_300) @[BankedStore.scala 164:45]
    node _T_305 = bits(reqs_0.bankSel, 1, 1) @[BankedStore.scala 165:33]
    node _T_306 = bits(reqs_1.bankSel, 1, 1) @[BankedStore.scala 165:33]
    node _T_307 = bits(reqs_2.bankSel, 1, 1) @[BankedStore.scala 165:33]
    node _T_308 = bits(reqs_3.bankSel, 1, 1) @[BankedStore.scala 165:33]
    node _T_309 = bits(reqs_4.bankSel, 1, 1) @[BankedStore.scala 165:33]
    node _T_310 = mux(_T_308, reqs_3.wen, reqs_4.wen) @[Mux.scala 47:69]
    node _T_311 = mux(_T_307, reqs_2.wen, _T_310) @[Mux.scala 47:69]
    node _T_312 = mux(_T_306, reqs_1.wen, _T_311) @[Mux.scala 47:69]
    node _T_313 = mux(_T_305, reqs_0.wen, _T_312) @[Mux.scala 47:69]
    node _T_314 = mux(_T_308, reqs_3.index, reqs_4.index) @[Mux.scala 47:69]
    node _T_315 = mux(_T_307, reqs_2.index, _T_314) @[Mux.scala 47:69]
    node _T_316 = mux(_T_306, reqs_1.index, _T_315) @[Mux.scala 47:69]
    node _T_317 = mux(_T_305, reqs_0.index, _T_316) @[Mux.scala 47:69]
    node _T_318 = mux(_T_308, reqs_3.data[1], reqs_4.data[1]) @[Mux.scala 47:69]
    node _T_319 = mux(_T_307, reqs_2.data[1], _T_318) @[Mux.scala 47:69]
    node _T_320 = mux(_T_306, reqs_1.data[1], _T_319) @[Mux.scala 47:69]
    node _T_321 = mux(_T_305, reqs_0.data[1], _T_320) @[Mux.scala 47:69]
    node _T_322 = and(_T_313, _T_304) @[BankedStore.scala 170:15]
    when _T_322 : @[BankedStore.scala 170:22]
      write mport _T_323 = cc_banks_1[_T_317], clock
      _T_323 <= _T_321
      skip @[BankedStore.scala 170:22]
    node _T_324 = eq(_T_313, UInt<1>("h00")) @[BankedStore.scala 171:27]
    node _T_325 = and(_T_324, _T_304) @[BankedStore.scala 171:32]
    wire _T_326 : UInt @[BankedStore.scala 171:21]
    _T_326 is invalid @[BankedStore.scala 171:21]
    _T_326 is invalid @[BankedStore.scala 171:21]
    when _T_325 : @[BankedStore.scala 171:21]
      _T_326 <= _T_317 @[BankedStore.scala 171:21]
      node _T_327 = or(_T_326, UInt<14>("h00")) @[BankedStore.scala 171:21]
      node _T_328 = bits(_T_327, 13, 0) @[BankedStore.scala 171:21]
      read mport _T_329 = cc_banks_1[_T_328], clock @[BankedStore.scala 171:21]
      skip @[BankedStore.scala 171:21]
    node _T_330 = eq(_T_313, UInt<1>("h00")) @[BankedStore.scala 171:48]
    node _T_331 = and(_T_330, _T_304) @[BankedStore.scala 171:53]
    reg _T_332 : UInt<1>, clock @[BankedStore.scala 171:47]
    _T_332 <= _T_331 @[BankedStore.scala 171:47]
    reg _T_333 : UInt<64>, clock @[Reg.scala 15:16]
    when _T_332 : @[Reg.scala 16:19]
      _T_333 <= _T_329 @[Reg.scala 16:23]
      skip @[Reg.scala 16:19]
    node _T_334 = bits(reqs_0.bankEn, 2, 2) @[BankedStore.scala 164:32]
    node _T_335 = bits(reqs_1.bankEn, 2, 2) @[BankedStore.scala 164:32]
    node _T_336 = bits(reqs_2.bankEn, 2, 2) @[BankedStore.scala 164:32]
    node _T_337 = bits(reqs_3.bankEn, 2, 2) @[BankedStore.scala 164:32]
    node _T_338 = bits(reqs_4.bankEn, 2, 2) @[BankedStore.scala 164:32]
    node _T_339 = or(_T_334, _T_335) @[BankedStore.scala 164:45]
    node _T_340 = or(_T_339, _T_336) @[BankedStore.scala 164:45]
    node _T_341 = or(_T_340, _T_337) @[BankedStore.scala 164:45]
    node _T_342 = or(_T_341, _T_338) @[BankedStore.scala 164:45]
    node _T_343 = bits(reqs_0.bankSel, 2, 2) @[BankedStore.scala 165:33]
    node _T_344 = bits(reqs_1.bankSel, 2, 2) @[BankedStore.scala 165:33]
    node _T_345 = bits(reqs_2.bankSel, 2, 2) @[BankedStore.scala 165:33]
    node _T_346 = bits(reqs_3.bankSel, 2, 2) @[BankedStore.scala 165:33]
    node _T_347 = bits(reqs_4.bankSel, 2, 2) @[BankedStore.scala 165:33]
    node _T_348 = mux(_T_346, reqs_3.wen, reqs_4.wen) @[Mux.scala 47:69]
    node _T_349 = mux(_T_345, reqs_2.wen, _T_348) @[Mux.scala 47:69]
    node _T_350 = mux(_T_344, reqs_1.wen, _T_349) @[Mux.scala 47:69]
    node _T_351 = mux(_T_343, reqs_0.wen, _T_350) @[Mux.scala 47:69]
    node _T_352 = mux(_T_346, reqs_3.index, reqs_4.index) @[Mux.scala 47:69]
    node _T_353 = mux(_T_345, reqs_2.index, _T_352) @[Mux.scala 47:69]
    node _T_354 = mux(_T_344, reqs_1.index, _T_353) @[Mux.scala 47:69]
    node _T_355 = mux(_T_343, reqs_0.index, _T_354) @[Mux.scala 47:69]
    node _T_356 = mux(_T_346, reqs_3.data[2], reqs_4.data[2]) @[Mux.scala 47:69]
    node _T_357 = mux(_T_345, reqs_2.data[2], _T_356) @[Mux.scala 47:69]
    node _T_358 = mux(_T_344, reqs_1.data[2], _T_357) @[Mux.scala 47:69]
    node _T_359 = mux(_T_343, reqs_0.data[2], _T_358) @[Mux.scala 47:69]
    node _T_360 = and(_T_351, _T_342) @[BankedStore.scala 170:15]
    when _T_360 : @[BankedStore.scala 170:22]
      write mport _T_361 = cc_banks_2[_T_355], clock
      _T_361 <= _T_359
      skip @[BankedStore.scala 170:22]
    node _T_362 = eq(_T_351, UInt<1>("h00")) @[BankedStore.scala 171:27]
    node _T_363 = and(_T_362, _T_342) @[BankedStore.scala 171:32]
    wire _T_364 : UInt @[BankedStore.scala 171:21]
    _T_364 is invalid @[BankedStore.scala 171:21]
    _T_364 is invalid @[BankedStore.scala 171:21]
    when _T_363 : @[BankedStore.scala 171:21]
      _T_364 <= _T_355 @[BankedStore.scala 171:21]
      node _T_365 = or(_T_364, UInt<14>("h00")) @[BankedStore.scala 171:21]
      node _T_366 = bits(_T_365, 13, 0) @[BankedStore.scala 171:21]
      read mport _T_367 = cc_banks_2[_T_366], clock @[BankedStore.scala 171:21]
      skip @[BankedStore.scala 171:21]
    node _T_368 = eq(_T_351, UInt<1>("h00")) @[BankedStore.scala 171:48]
    node _T_369 = and(_T_368, _T_342) @[BankedStore.scala 171:53]
    reg _T_370 : UInt<1>, clock @[BankedStore.scala 171:47]
    _T_370 <= _T_369 @[BankedStore.scala 171:47]
    reg _T_371 : UInt<64>, clock @[Reg.scala 15:16]
    when _T_370 : @[Reg.scala 16:19]
      _T_371 <= _T_367 @[Reg.scala 16:23]
      skip @[Reg.scala 16:19]
    node _T_372 = bits(reqs_0.bankEn, 3, 3) @[BankedStore.scala 164:32]
    node _T_373 = bits(reqs_1.bankEn, 3, 3) @[BankedStore.scala 164:32]
    node _T_374 = bits(reqs_2.bankEn, 3, 3) @[BankedStore.scala 164:32]
    node _T_375 = bits(reqs_3.bankEn, 3, 3) @[BankedStore.scala 164:32]
    node _T_376 = bits(reqs_4.bankEn, 3, 3) @[BankedStore.scala 164:32]
    node _T_377 = or(_T_372, _T_373) @[BankedStore.scala 164:45]
    node _T_378 = or(_T_377, _T_374) @[BankedStore.scala 164:45]
    node _T_379 = or(_T_378, _T_375) @[BankedStore.scala 164:45]
    node _T_380 = or(_T_379, _T_376) @[BankedStore.scala 164:45]
    node _T_381 = bits(reqs_0.bankSel, 3, 3) @[BankedStore.scala 165:33]
    node _T_382 = bits(reqs_1.bankSel, 3, 3) @[BankedStore.scala 165:33]
    node _T_383 = bits(reqs_2.bankSel, 3, 3) @[BankedStore.scala 165:33]
    node _T_384 = bits(reqs_3.bankSel, 3, 3) @[BankedStore.scala 165:33]
    node _T_385 = bits(reqs_4.bankSel, 3, 3) @[BankedStore.scala 165:33]
    node _T_386 = mux(_T_384, reqs_3.wen, reqs_4.wen) @[Mux.scala 47:69]
    node _T_387 = mux(_T_383, reqs_2.wen, _T_386) @[Mux.scala 47:69]
    node _T_388 = mux(_T_382, reqs_1.wen, _T_387) @[Mux.scala 47:69]
    node _T_389 = mux(_T_381, reqs_0.wen, _T_388) @[Mux.scala 47:69]
    node _T_390 = mux(_T_384, reqs_3.index, reqs_4.index) @[Mux.scala 47:69]
    node _T_391 = mux(_T_383, reqs_2.index, _T_390) @[Mux.scala 47:69]
    node _T_392 = mux(_T_382, reqs_1.index, _T_391) @[Mux.scala 47:69]
    node _T_393 = mux(_T_381, reqs_0.index, _T_392) @[Mux.scala 47:69]
    node _T_394 = mux(_T_384, reqs_3.data[3], reqs_4.data[3]) @[Mux.scala 47:69]
    node _T_395 = mux(_T_383, reqs_2.data[3], _T_394) @[Mux.scala 47:69]
    node _T_396 = mux(_T_382, reqs_1.data[3], _T_395) @[Mux.scala 47:69]
    node _T_397 = mux(_T_381, reqs_0.data[3], _T_396) @[Mux.scala 47:69]
    node _T_398 = and(_T_389, _T_380) @[BankedStore.scala 170:15]
    when _T_398 : @[BankedStore.scala 170:22]
      write mport _T_399 = cc_banks_3[_T_393], clock
      _T_399 <= _T_397
      skip @[BankedStore.scala 170:22]
    node _T_400 = eq(_T_389, UInt<1>("h00")) @[BankedStore.scala 171:27]
    node _T_401 = and(_T_400, _T_380) @[BankedStore.scala 171:32]
    wire _T_402 : UInt @[BankedStore.scala 171:21]
    _T_402 is invalid @[BankedStore.scala 171:21]
    _T_402 is invalid @[BankedStore.scala 171:21]
    when _T_401 : @[BankedStore.scala 171:21]
      _T_402 <= _T_393 @[BankedStore.scala 171:21]
      node _T_403 = or(_T_402, UInt<14>("h00")) @[BankedStore.scala 171:21]
      node _T_404 = bits(_T_403, 13, 0) @[BankedStore.scala 171:21]
      read mport _T_405 = cc_banks_3[_T_404], clock @[BankedStore.scala 171:21]
      skip @[BankedStore.scala 171:21]
    node _T_406 = eq(_T_389, UInt<1>("h00")) @[BankedStore.scala 171:48]
    node _T_407 = and(_T_406, _T_380) @[BankedStore.scala 171:53]
    reg _T_408 : UInt<1>, clock @[BankedStore.scala 171:47]
    _T_408 <= _T_407 @[BankedStore.scala 171:47]
    reg _T_409 : UInt<64>, clock @[Reg.scala 15:16]
    when _T_408 : @[Reg.scala 16:19]
      _T_409 <= _T_405 @[Reg.scala 16:23]
      skip @[Reg.scala 16:19]
    wire regout : UInt<64>[4] @[BankedStore.scala 163:19]
    regout is invalid @[BankedStore.scala 163:19]
    regout[0] <= _T_295 @[BankedStore.scala 163:19]
    regout[1] <= _T_333 @[BankedStore.scala 163:19]
    regout[2] <= _T_371 @[BankedStore.scala 163:19]
    regout[3] <= _T_409 @[BankedStore.scala 163:19]
    reg _T_410 : UInt, clock @[BankedStore.scala 174:39]
    _T_410 <= reqs_1.bankEn @[BankedStore.scala 174:39]
    reg regsel_sourceC : UInt, clock @[BankedStore.scala 174:31]
    regsel_sourceC <= _T_410 @[BankedStore.scala 174:31]
    reg _T_411 : UInt, clock @[BankedStore.scala 175:39]
    _T_411 <= reqs_4.bankEn @[BankedStore.scala 175:39]
    reg regsel_sourceD : UInt, clock @[BankedStore.scala 175:31]
    regsel_sourceD <= _T_411 @[BankedStore.scala 175:31]
    node _T_412 = bits(regsel_sourceC, 0, 0) @[BankedStore.scala 178:38]
    node _T_413 = mux(_T_412, regout[0], UInt<1>("h00")) @[BankedStore.scala 178:23]
    node _T_414 = bits(regsel_sourceC, 1, 1) @[BankedStore.scala 178:38]
    node _T_415 = mux(_T_414, regout[1], UInt<1>("h00")) @[BankedStore.scala 178:23]
    node _T_416 = bits(regsel_sourceC, 2, 2) @[BankedStore.scala 178:38]
    node _T_417 = mux(_T_416, regout[2], UInt<1>("h00")) @[BankedStore.scala 178:23]
    node _T_418 = bits(regsel_sourceC, 3, 3) @[BankedStore.scala 178:38]
    node _T_419 = mux(_T_418, regout[3], UInt<1>("h00")) @[BankedStore.scala 178:23]
    node _T_420 = or(_T_413, _T_415) @[BankedStore.scala 179:85]
    node _T_421 = or(_T_420, _T_417) @[BankedStore.scala 179:85]
    node decodeC_0 = or(_T_421, _T_419) @[BankedStore.scala 179:85]
    io.sourceC_dat.data <= decodeC_0 @[BankedStore.scala 181:23]
    node _T_422 = bits(regsel_sourceD, 0, 0) @[BankedStore.scala 185:38]
    node _T_423 = mux(_T_422, regout[0], UInt<1>("h00")) @[BankedStore.scala 185:23]
    node _T_424 = bits(regsel_sourceD, 1, 1) @[BankedStore.scala 185:38]
    node _T_425 = mux(_T_424, regout[1], UInt<1>("h00")) @[BankedStore.scala 185:23]
    node _T_426 = bits(regsel_sourceD, 2, 2) @[BankedStore.scala 185:38]
    node _T_427 = mux(_T_426, regout[2], UInt<1>("h00")) @[BankedStore.scala 185:23]
    node _T_428 = bits(regsel_sourceD, 3, 3) @[BankedStore.scala 185:38]
    node _T_429 = mux(_T_428, regout[3], UInt<1>("h00")) @[BankedStore.scala 185:23]
    node _T_430 = or(_T_423, _T_425) @[BankedStore.scala 186:85]
    node _T_431 = or(_T_430, _T_427) @[BankedStore.scala 186:85]
    node decodeD_0 = or(_T_431, _T_429) @[BankedStore.scala 186:85]
    io.sourceD_rdat.data <= decodeD_0 @[BankedStore.scala 188:24]
