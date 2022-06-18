circuit RenameBusyTable :
  module RenameBusyTable : 
    input clock : Clock
    input reset : UInt<1>
    output io : {flip ren_uops : {uopc : UInt<7>, inst : UInt<32>, debug_inst : UInt<32>, is_rvc : UInt<1>, debug_pc : UInt<40>, iq_type : UInt<3>, fu_code : UInt<10>, ctrl : {br_type : UInt<4>, op1_sel : UInt<2>, op2_sel : UInt<3>, imm_sel : UInt<3>, op_fcn : UInt<4>, fcn_dw : UInt<1>, csr_cmd : UInt<3>, is_load : UInt<1>, is_sta : UInt<1>, is_std : UInt<1>}, iw_state : UInt<2>, iw_p1_poisoned : UInt<1>, iw_p2_poisoned : UInt<1>, is_br : UInt<1>, is_jalr : UInt<1>, is_jal : UInt<1>, is_sfb : UInt<1>, br_mask : UInt<8>, br_tag : UInt<3>, ftq_idx : UInt<4>, edge_inst : UInt<1>, pc_lob : UInt<6>, taken : UInt<1>, imm_packed : UInt<20>, csr_addr : UInt<12>, rob_idx : UInt<5>, ldq_idx : UInt<3>, stq_idx : UInt<3>, rxq_idx : UInt<2>, pdst : UInt<6>, prs1 : UInt<6>, prs2 : UInt<6>, prs3 : UInt<6>, ppred : UInt<4>, prs1_busy : UInt<1>, prs2_busy : UInt<1>, prs3_busy : UInt<1>, ppred_busy : UInt<1>, stale_pdst : UInt<6>, exception : UInt<1>, exc_cause : UInt<64>, bypassable : UInt<1>, mem_cmd : UInt<5>, mem_size : UInt<2>, mem_signed : UInt<1>, is_fence : UInt<1>, is_fencei : UInt<1>, is_amo : UInt<1>, uses_ldq : UInt<1>, uses_stq : UInt<1>, is_sys_pc2epc : UInt<1>, is_unique : UInt<1>, flush_on_commit : UInt<1>, ldst_is_rs1 : UInt<1>, ldst : UInt<6>, lrs1 : UInt<6>, lrs2 : UInt<6>, lrs3 : UInt<6>, ldst_val : UInt<1>, dst_rtype : UInt<2>, lrs1_rtype : UInt<2>, lrs2_rtype : UInt<2>, frs3_en : UInt<1>, fp_val : UInt<1>, fp_single : UInt<1>, xcpt_pf_if : UInt<1>, xcpt_ae_if : UInt<1>, xcpt_ma_if : UInt<1>, bp_debug_if : UInt<1>, bp_xcpt_if : UInt<1>, debug_fsrc : UInt<2>, debug_tsrc : UInt<2>}[1], busy_resps : {prs1_busy : UInt<1>, prs2_busy : UInt<1>, prs3_busy : UInt<1>}[1], flip rebusy_reqs : UInt<1>[1], flip wb_pdsts : UInt<6>[3], flip wb_valids : UInt<1>[3], debug : {busytable : UInt<52>}}
    
    clock is invalid
    reset is invalid
    io is invalid
    reg busy_table : UInt<52>, clock with : (reset => (reset, UInt<52>("h00"))) @[rename-busytable.scala 48:27]
    node _T = dshl(UInt<1>("h01"), io.wb_pdsts[0]) @[OneHot.scala 58:35]
    node _T_1 = bits(io.wb_valids[0], 0, 0) @[Bitwise.scala 72:15]
    node _T_2 = mux(_T_1, UInt<52>("h0fffffffffffff"), UInt<52>("h00")) @[Bitwise.scala 72:12]
    node _T_3 = and(_T, _T_2) @[rename-busytable.scala 51:48]
    node _T_4 = dshl(UInt<1>("h01"), io.wb_pdsts[1]) @[OneHot.scala 58:35]
    node _T_5 = bits(io.wb_valids[1], 0, 0) @[Bitwise.scala 72:15]
    node _T_6 = mux(_T_5, UInt<52>("h0fffffffffffff"), UInt<52>("h00")) @[Bitwise.scala 72:12]
    node _T_7 = and(_T_4, _T_6) @[rename-busytable.scala 51:48]
    node _T_8 = dshl(UInt<1>("h01"), io.wb_pdsts[2]) @[OneHot.scala 58:35]
    node _T_9 = bits(io.wb_valids[2], 0, 0) @[Bitwise.scala 72:15]
    node _T_10 = mux(_T_9, UInt<52>("h0fffffffffffff"), UInt<52>("h00")) @[Bitwise.scala 72:12]
    node _T_11 = and(_T_8, _T_10) @[rename-busytable.scala 51:48]
    node _T_12 = or(_T_3, _T_7) @[rename-busytable.scala 51:88]
    node _T_13 = or(_T_12, _T_11) @[rename-busytable.scala 51:88]
    node _T_14 = not(_T_13) @[rename-busytable.scala 50:36]
    node busy_table_wb = and(busy_table, _T_14) @[rename-busytable.scala 50:34]
    node _T_15 = dshl(UInt<1>("h01"), io.ren_uops[0].pdst) @[OneHot.scala 58:35]
    node _T_16 = bits(io.rebusy_reqs[0], 0, 0) @[Bitwise.scala 72:15]
    node _T_17 = mux(_T_16, UInt<52>("h0fffffffffffff"), UInt<52>("h00")) @[Bitwise.scala 72:12]
    node _T_18 = and(_T_15, _T_17) @[rename-busytable.scala 54:49]
    node busy_table_next = or(busy_table_wb, _T_18) @[rename-busytable.scala 53:39]
    busy_table <= busy_table_next @[rename-busytable.scala 56:14]
    node _T_19 = dshr(busy_table, io.ren_uops[0].prs1) @[rename-busytable.scala 67:45]
    node _T_20 = bits(_T_19, 0, 0) @[rename-busytable.scala 67:45]
    node _T_21 = and(UInt<1>("h00"), UInt<1>("h00")) @[rename-busytable.scala 67:88]
    node _T_22 = or(_T_20, _T_21) @[rename-busytable.scala 67:67]
    io.busy_resps[0].prs1_busy <= _T_22 @[rename-busytable.scala 67:32]
    node _T_23 = dshr(busy_table, io.ren_uops[0].prs2) @[rename-busytable.scala 68:45]
    node _T_24 = bits(_T_23, 0, 0) @[rename-busytable.scala 68:45]
    node _T_25 = and(UInt<1>("h00"), UInt<1>("h00")) @[rename-busytable.scala 68:88]
    node _T_26 = or(_T_24, _T_25) @[rename-busytable.scala 68:67]
    io.busy_resps[0].prs2_busy <= _T_26 @[rename-busytable.scala 68:32]
    node _T_27 = dshr(busy_table, io.ren_uops[0].prs3) @[rename-busytable.scala 69:45]
    node _T_28 = bits(_T_27, 0, 0) @[rename-busytable.scala 69:45]
    node _T_29 = and(UInt<1>("h00"), UInt<1>("h00")) @[rename-busytable.scala 69:88]
    node _T_30 = or(_T_28, _T_29) @[rename-busytable.scala 69:67]
    io.busy_resps[0].prs3_busy <= _T_30 @[rename-busytable.scala 69:32]
    io.busy_resps[0].prs3_busy <= UInt<1>("h00") @[rename-busytable.scala 70:44]
    io.debug.busytable <= busy_table @[rename-busytable.scala 73:22]
    
