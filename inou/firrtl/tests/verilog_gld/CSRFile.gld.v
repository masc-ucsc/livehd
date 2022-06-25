module CSRFile(
  input         clock,
  input         reset,
  input         io_ungated_clock,
  input         io_interrupts_debug,
  input         io_interrupts_mtip,
  input         io_interrupts_msip,
  input         io_interrupts_meip,
  input         io_interrupts_seip,
  input         io_hartid,
  input  [11:0] io_rw_addr,
  input  [2:0]  io_rw_cmd,
  output [63:0] io_rw_rdata,
  input  [63:0] io_rw_wdata,
  input  [11:0] io_decode_0_csr,
  output        io_decode_0_fp_illegal,
  output        io_decode_0_vector_illegal,
  output        io_decode_0_fp_csr,
  output        io_decode_0_rocc_illegal,
  output        io_decode_0_read_illegal,
  output        io_decode_0_write_illegal,
  output        io_decode_0_write_flush,
  output        io_decode_0_system_illegal,
  output        io_csr_stall,
  output        io_eret,
  output        io_singleStep,
  output        io_status_debug,
  output        io_status_cease,
  output        io_status_wfi,
  output [31:0] io_status_isa,
  output [1:0]  io_status_dprv,
  output [1:0]  io_status_prv,
  output        io_status_sd,
  output [26:0] io_status_zero2,
  output [1:0]  io_status_sxl,
  output [1:0]  io_status_uxl,
  output        io_status_sd_rv32,
  output [7:0]  io_status_zero1,
  output        io_status_tsr,
  output        io_status_tw,
  output        io_status_tvm,
  output        io_status_mxr,
  output        io_status_sum,
  output        io_status_mprv,
  output [1:0]  io_status_xs,
  output [1:0]  io_status_fs,
  output [1:0]  io_status_mpp,
  output [1:0]  io_status_vs,
  output        io_status_spp,
  output        io_status_mpie,
  output        io_status_hpie,
  output        io_status_spie,
  output        io_status_upie,
  output        io_status_mie,
  output        io_status_hie,
  output        io_status_sie,
  output        io_status_uie,
  output [3:0]  io_ptbr_mode,
  output [15:0] io_ptbr_asid,
  output [43:0] io_ptbr_ppn,
  output [39:0] io_evec,
  input         io_exception,
  input         io_retire,
  input  [63:0] io_cause,
  input  [39:0] io_pc,
  input  [39:0] io_tval,
  output [63:0] io_time,
  output [2:0]  io_fcsr_rm,
  input         io_fcsr_flags_valid,
  input  [4:0]  io_fcsr_flags_bits,
  input         io_set_fs_dirty,
  input         io_rocc_interrupt,
  output        io_interrupt,
  output [63:0] io_interrupt_cause,
  output        io_pmp_0_cfg_l,
  output [1:0]  io_pmp_0_cfg_res,
  output [1:0]  io_pmp_0_cfg_a,
  output        io_pmp_0_cfg_x,
  output        io_pmp_0_cfg_w,
  output        io_pmp_0_cfg_r,
  output [29:0] io_pmp_0_addr,
  output [31:0] io_pmp_0_mask,
  output        io_pmp_1_cfg_l,
  output [1:0]  io_pmp_1_cfg_res,
  output [1:0]  io_pmp_1_cfg_a,
  output        io_pmp_1_cfg_x,
  output        io_pmp_1_cfg_w,
  output        io_pmp_1_cfg_r,
  output [29:0] io_pmp_1_addr,
  output [31:0] io_pmp_1_mask,
  output        io_pmp_2_cfg_l,
  output [1:0]  io_pmp_2_cfg_res,
  output [1:0]  io_pmp_2_cfg_a,
  output        io_pmp_2_cfg_x,
  output        io_pmp_2_cfg_w,
  output        io_pmp_2_cfg_r,
  output [29:0] io_pmp_2_addr,
  output [31:0] io_pmp_2_mask,
  output        io_pmp_3_cfg_l,
  output [1:0]  io_pmp_3_cfg_res,
  output [1:0]  io_pmp_3_cfg_a,
  output        io_pmp_3_cfg_x,
  output        io_pmp_3_cfg_w,
  output        io_pmp_3_cfg_r,
  output [29:0] io_pmp_3_addr,
  output [31:0] io_pmp_3_mask,
  output        io_pmp_4_cfg_l,
  output [1:0]  io_pmp_4_cfg_res,
  output [1:0]  io_pmp_4_cfg_a,
  output        io_pmp_4_cfg_x,
  output        io_pmp_4_cfg_w,
  output        io_pmp_4_cfg_r,
  output [29:0] io_pmp_4_addr,
  output [31:0] io_pmp_4_mask,
  output        io_pmp_5_cfg_l,
  output [1:0]  io_pmp_5_cfg_res,
  output [1:0]  io_pmp_5_cfg_a,
  output        io_pmp_5_cfg_x,
  output        io_pmp_5_cfg_w,
  output        io_pmp_5_cfg_r,
  output [29:0] io_pmp_5_addr,
  output [31:0] io_pmp_5_mask,
  output        io_pmp_6_cfg_l,
  output [1:0]  io_pmp_6_cfg_res,
  output [1:0]  io_pmp_6_cfg_a,
  output        io_pmp_6_cfg_x,
  output        io_pmp_6_cfg_w,
  output        io_pmp_6_cfg_r,
  output [29:0] io_pmp_6_addr,
  output [31:0] io_pmp_6_mask,
  output        io_pmp_7_cfg_l,
  output [1:0]  io_pmp_7_cfg_res,
  output [1:0]  io_pmp_7_cfg_a,
  output        io_pmp_7_cfg_x,
  output        io_pmp_7_cfg_w,
  output        io_pmp_7_cfg_r,
  output [29:0] io_pmp_7_addr,
  output [31:0] io_pmp_7_mask,
  output [63:0] io_counters_0_eventSel,
  input         io_counters_0_inc,
  output [63:0] io_counters_1_eventSel,
  input         io_counters_1_inc,
  output [31:0] io_csrw_counter,
  input  [31:0] io_inst_0,
  output        io_trace_0_valid,
  output [39:0] io_trace_0_iaddr,
  output [31:0] io_trace_0_insn,
  output [2:0]  io_trace_0_priv,
  output        io_trace_0_exception,
  output        io_trace_0_interrupt,
  output [7:0]  io_trace_0_cause,
  output [39:0] io_trace_0_tval,
  output        io_customCSRs_0_wen,
  output [63:0] io_customCSRs_0_wdata,
  output [63:0] io_customCSRs_0_value
);
`ifdef RANDOMIZE_REG_INIT
  reg [31:0] _RAND_0;
  reg [31:0] _RAND_1;
  reg [31:0] _RAND_2;
  reg [31:0] _RAND_3;
  reg [31:0] _RAND_4;
  reg [31:0] _RAND_5;
  reg [31:0] _RAND_6;
  reg [31:0] _RAND_7;
  reg [31:0] _RAND_8;
  reg [31:0] _RAND_9;
  reg [31:0] _RAND_10;
  reg [31:0] _RAND_11;
  reg [31:0] _RAND_12;
  reg [31:0] _RAND_13;
  reg [31:0] _RAND_14;
  reg [31:0] _RAND_15;
  reg [31:0] _RAND_16;
  reg [31:0] _RAND_17;
  reg [31:0] _RAND_18;
  reg [31:0] _RAND_19;
  reg [63:0] _RAND_20;
  reg [63:0] _RAND_21;
  reg [31:0] _RAND_22;
  reg [31:0] _RAND_23;
  reg [63:0] _RAND_24;
  reg [63:0] _RAND_25;
  reg [31:0] _RAND_26;
  reg [31:0] _RAND_27;
  reg [31:0] _RAND_28;
  reg [31:0] _RAND_29;
  reg [31:0] _RAND_30;
  reg [31:0] _RAND_31;
  reg [31:0] _RAND_32;
  reg [31:0] _RAND_33;
  reg [31:0] _RAND_34;
  reg [31:0] _RAND_35;
  reg [31:0] _RAND_36;
  reg [31:0] _RAND_37;
  reg [31:0] _RAND_38;
  reg [31:0] _RAND_39;
  reg [31:0] _RAND_40;
  reg [31:0] _RAND_41;
  reg [31:0] _RAND_42;
  reg [31:0] _RAND_43;
  reg [31:0] _RAND_44;
  reg [31:0] _RAND_45;
  reg [31:0] _RAND_46;
  reg [31:0] _RAND_47;
  reg [31:0] _RAND_48;
  reg [31:0] _RAND_49;
  reg [31:0] _RAND_50;
  reg [31:0] _RAND_51;
  reg [31:0] _RAND_52;
  reg [31:0] _RAND_53;
  reg [31:0] _RAND_54;
  reg [31:0] _RAND_55;
  reg [31:0] _RAND_56;
  reg [31:0] _RAND_57;
  reg [31:0] _RAND_58;
  reg [31:0] _RAND_59;
  reg [31:0] _RAND_60;
  reg [31:0] _RAND_61;
  reg [31:0] _RAND_62;
  reg [31:0] _RAND_63;
  reg [31:0] _RAND_64;
  reg [31:0] _RAND_65;
  reg [31:0] _RAND_66;
  reg [31:0] _RAND_67;
  reg [31:0] _RAND_68;
  reg [31:0] _RAND_69;
  reg [31:0] _RAND_70;
  reg [31:0] _RAND_71;
  reg [31:0] _RAND_72;
  reg [31:0] _RAND_73;
  reg [63:0] _RAND_74;
  reg [31:0] _RAND_75;
  reg [31:0] _RAND_76;
  reg [31:0] _RAND_77;
  reg [63:0] _RAND_78;
  reg [63:0] _RAND_79;
  reg [63:0] _RAND_80;
  reg [63:0] _RAND_81;
  reg [31:0] _RAND_82;
  reg [31:0] _RAND_83;
  reg [31:0] _RAND_84;
  reg [63:0] _RAND_85;
  reg [63:0] _RAND_86;
  reg [63:0] _RAND_87;
  reg [63:0] _RAND_88;
  reg [63:0] _RAND_89;
  reg [31:0] _RAND_90;
  reg [63:0] _RAND_91;
  reg [31:0] _RAND_92;
  reg [31:0] _RAND_93;
  reg [31:0] _RAND_94;
  reg [31:0] _RAND_95;
  reg [63:0] _RAND_96;
  reg [31:0] _RAND_97;
  reg [63:0] _RAND_98;
  reg [63:0] _RAND_99;
  reg [63:0] _RAND_100;
  reg [31:0] _RAND_101;
  reg [63:0] _RAND_102;
  reg [31:0] _RAND_103;
  reg [63:0] _RAND_104;
  reg [63:0] _RAND_105;
  reg [31:0] _RAND_106;
  reg [31:0] _RAND_107;
`endif // RANDOMIZE_REG_INIT
  reg [1:0] reg_mstatus_prv; // @[CSR.scala 300:24]
  reg  reg_mstatus_tsr; // @[CSR.scala 300:24]
  reg  reg_mstatus_tw; // @[CSR.scala 300:24]
  reg  reg_mstatus_tvm; // @[CSR.scala 300:24]
  reg  reg_mstatus_mxr; // @[CSR.scala 300:24]
  reg  reg_mstatus_sum; // @[CSR.scala 300:24]
  reg  reg_mstatus_mprv; // @[CSR.scala 300:24]
  reg [1:0] reg_mstatus_fs; // @[CSR.scala 300:24]
  reg [1:0] reg_mstatus_mpp; // @[CSR.scala 300:24]
  reg  reg_mstatus_spp; // @[CSR.scala 300:24]
  reg  reg_mstatus_mpie; // @[CSR.scala 300:24]
  reg  reg_mstatus_spie; // @[CSR.scala 300:24]
  reg  reg_mstatus_mie; // @[CSR.scala 300:24]
  reg  reg_mstatus_sie; // @[CSR.scala 300:24]
  wire  system_insn = io_rw_cmd == 3'h4; // @[CSR.scala 577:31]
  wire [31:0] _T_717 = {io_rw_addr, 20'h0}; // @[CSR.scala 589:28]
  wire [31:0] _T_724 = _T_717 & 32'h12400000; // @[Decode.scala 14:65]
  wire  _T_725 = _T_724 == 32'h10000000; // @[Decode.scala 14:121]
  wire [31:0] _T_726 = _T_717 & 32'h40000000; // @[Decode.scala 14:65]
  wire  _T_727 = _T_726 == 32'h40000000; // @[Decode.scala 14:121]
  wire  _T_729 = _T_725 | _T_727; // @[Decode.scala 15:30]
  wire  insn_ret = system_insn & _T_729; // @[CSR.scala 589:95]
  reg [1:0] reg_dcsr_prv; // @[CSR.scala 308:21]
  wire [1:0] _GEN_95 = io_rw_addr[10] ? reg_dcsr_prv : reg_mstatus_mpp; // @[CSR.scala 733:53 734:15 741:15]
  wire [1:0] _GEN_104 = ~io_rw_addr[9] ? {{1'd0}, reg_mstatus_spp} : _GEN_95; // @[CSR.scala 727:52 731:15]
  wire [31:0] _T_718 = _T_717 & 32'h10100000; // @[Decode.scala 14:65]
  wire  _T_719 = _T_718 == 32'h0; // @[Decode.scala 14:121]
  wire  insn_call = system_insn & _T_719; // @[CSR.scala 589:95]
  wire  _T_722 = _T_718 == 32'h100000; // @[Decode.scala 14:121]
  wire  insn_break = system_insn & _T_722; // @[CSR.scala 589:95]
  wire  _T_1211 = insn_call | insn_break; // @[CSR.scala 661:29]
  wire  exception = insn_call | insn_break | io_exception; // @[CSR.scala 661:43]
  reg  reg_singleStepped; // @[CSR.scala 352:30]
  wire [3:0] _GEN_390 = {{2'd0}, reg_mstatus_prv}; // @[CSR.scala 625:36]
  wire [3:0] _T_1157 = _GEN_390 + 4'h8; // @[CSR.scala 625:36]
  wire [63:0] _T_1158 = insn_break ? 64'h3 : io_cause; // @[CSR.scala 626:14]
  wire [63:0] cause = insn_call ? {{60'd0}, _T_1157} : _T_1158; // @[CSR.scala 625:8]
  wire [7:0] cause_lsbs = cause[7:0]; // @[CSR.scala 627:25]
  wire  _T_1160 = cause_lsbs == 8'he; // @[CSR.scala 628:53]
  wire  causeIsDebugInt = cause[63] & cause_lsbs == 8'he; // @[CSR.scala 628:39]
  wire  _T_1162 = ~cause[63]; // @[CSR.scala 629:29]
  wire  causeIsDebugTrigger = ~cause[63] & _T_1160; // @[CSR.scala 629:44]
  reg  reg_dcsr_ebreakm; // @[CSR.scala 308:21]
  reg  reg_dcsr_ebreaks; // @[CSR.scala 308:21]
  reg  reg_dcsr_ebreaku; // @[CSR.scala 308:21]
  wire [3:0] _T_1169 = {reg_dcsr_ebreakm,1'h0,reg_dcsr_ebreaks,reg_dcsr_ebreaku}; // @[Cat.scala 29:58]
  wire [3:0] _T_1170 = _T_1169 >> reg_mstatus_prv; // @[CSR.scala 630:134]
  wire  causeIsDebugBreak = _T_1162 & insn_break & _T_1170[0]; // @[CSR.scala 630:56]
  reg  reg_debug; // @[CSR.scala 349:22]
  wire  trapToDebug = reg_singleStepped | causeIsDebugInt | causeIsDebugTrigger | causeIsDebugBreak | reg_debug; // @[CSR.scala 631:123]
  wire  _T_1246 = ~reg_debug; // @[CSR.scala 678:13]
  wire [1:0] _GEN_44 = ~reg_debug ? 2'h3 : reg_mstatus_prv; // @[CSR.scala 678:25 683:17]
  wire  _T_1177 = reg_mstatus_prv <= 2'h1; // @[CSR.scala 633:59]
  reg [63:0] reg_mideleg; // @[CSR.scala 360:18]
  wire [63:0] read_mideleg = reg_mideleg & 64'h222; // @[CSR.scala 361:36]
  wire [63:0] _T_1180 = read_mideleg >> cause_lsbs; // @[CSR.scala 633:102]
  reg [63:0] reg_medeleg; // @[CSR.scala 364:18]
  wire [63:0] read_medeleg = reg_medeleg & 64'hb15d; // @[CSR.scala 365:36]
  wire [63:0] _T_1182 = read_medeleg >> cause_lsbs; // @[CSR.scala 633:128]
  wire  _T_1184 = cause[63] ? _T_1180[0] : _T_1182[0]; // @[CSR.scala 633:74]
  wire  delegate = reg_mstatus_prv <= 2'h1 & _T_1184; // @[CSR.scala 633:68]
  wire [1:0] _GEN_52 = delegate ? 2'h1 : 2'h3; // @[CSR.scala 685:27 693:15 702:15]
  wire [1:0] _GEN_63 = trapToDebug ? _GEN_44 : _GEN_52; // @[CSR.scala 677:24]
  wire [1:0] _GEN_81 = exception ? _GEN_63 : reg_mstatus_prv; // @[CSR.scala 676:20]
  wire [1:0] new_prv = insn_ret ? _GEN_104 : _GEN_81; // @[CSR.scala 726:19]
  reg [2:0] reg_dcsr_cause; // @[CSR.scala 308:21]
  reg  reg_dcsr_step; // @[CSR.scala 308:21]
  reg [39:0] reg_dpc; // @[CSR.scala 350:20]
  reg [63:0] reg_dscratch; // @[CSR.scala 351:25]
  reg  reg_pmp_0_cfg_l; // @[CSR.scala 356:20]
  reg [1:0] reg_pmp_0_cfg_a; // @[CSR.scala 356:20]
  reg  reg_pmp_0_cfg_x; // @[CSR.scala 356:20]
  reg  reg_pmp_0_cfg_w; // @[CSR.scala 356:20]
  reg  reg_pmp_0_cfg_r; // @[CSR.scala 356:20]
  reg [29:0] reg_pmp_0_addr; // @[CSR.scala 356:20]
  reg  reg_pmp_1_cfg_l; // @[CSR.scala 356:20]
  reg [1:0] reg_pmp_1_cfg_a; // @[CSR.scala 356:20]
  reg  reg_pmp_1_cfg_x; // @[CSR.scala 356:20]
  reg  reg_pmp_1_cfg_w; // @[CSR.scala 356:20]
  reg  reg_pmp_1_cfg_r; // @[CSR.scala 356:20]
  reg [29:0] reg_pmp_1_addr; // @[CSR.scala 356:20]
  reg  reg_pmp_2_cfg_l; // @[CSR.scala 356:20]
  reg [1:0] reg_pmp_2_cfg_a; // @[CSR.scala 356:20]
  reg  reg_pmp_2_cfg_x; // @[CSR.scala 356:20]
  reg  reg_pmp_2_cfg_w; // @[CSR.scala 356:20]
  reg  reg_pmp_2_cfg_r; // @[CSR.scala 356:20]
  reg [29:0] reg_pmp_2_addr; // @[CSR.scala 356:20]
  reg  reg_pmp_3_cfg_l; // @[CSR.scala 356:20]
  reg [1:0] reg_pmp_3_cfg_a; // @[CSR.scala 356:20]
  reg  reg_pmp_3_cfg_x; // @[CSR.scala 356:20]
  reg  reg_pmp_3_cfg_w; // @[CSR.scala 356:20]
  reg  reg_pmp_3_cfg_r; // @[CSR.scala 356:20]
  reg [29:0] reg_pmp_3_addr; // @[CSR.scala 356:20]
  reg  reg_pmp_4_cfg_l; // @[CSR.scala 356:20]
  reg [1:0] reg_pmp_4_cfg_a; // @[CSR.scala 356:20]
  reg  reg_pmp_4_cfg_x; // @[CSR.scala 356:20]
  reg  reg_pmp_4_cfg_w; // @[CSR.scala 356:20]
  reg  reg_pmp_4_cfg_r; // @[CSR.scala 356:20]
  reg [29:0] reg_pmp_4_addr; // @[CSR.scala 356:20]
  reg  reg_pmp_5_cfg_l; // @[CSR.scala 356:20]
  reg [1:0] reg_pmp_5_cfg_a; // @[CSR.scala 356:20]
  reg  reg_pmp_5_cfg_x; // @[CSR.scala 356:20]
  reg  reg_pmp_5_cfg_w; // @[CSR.scala 356:20]
  reg  reg_pmp_5_cfg_r; // @[CSR.scala 356:20]
  reg [29:0] reg_pmp_5_addr; // @[CSR.scala 356:20]
  reg  reg_pmp_6_cfg_l; // @[CSR.scala 356:20]
  reg [1:0] reg_pmp_6_cfg_a; // @[CSR.scala 356:20]
  reg  reg_pmp_6_cfg_x; // @[CSR.scala 356:20]
  reg  reg_pmp_6_cfg_w; // @[CSR.scala 356:20]
  reg  reg_pmp_6_cfg_r; // @[CSR.scala 356:20]
  reg [29:0] reg_pmp_6_addr; // @[CSR.scala 356:20]
  reg  reg_pmp_7_cfg_l; // @[CSR.scala 356:20]
  reg [1:0] reg_pmp_7_cfg_a; // @[CSR.scala 356:20]
  reg  reg_pmp_7_cfg_x; // @[CSR.scala 356:20]
  reg  reg_pmp_7_cfg_w; // @[CSR.scala 356:20]
  reg  reg_pmp_7_cfg_r; // @[CSR.scala 356:20]
  reg [29:0] reg_pmp_7_addr; // @[CSR.scala 356:20]
  reg [63:0] reg_mie; // @[CSR.scala 358:20]
  reg  reg_mip_seip; // @[CSR.scala 367:20]
  reg  reg_mip_stip; // @[CSR.scala 367:20]
  reg  reg_mip_ssip; // @[CSR.scala 367:20]
  reg [39:0] reg_mepc; // @[CSR.scala 368:21]
  reg [63:0] reg_mcause; // @[CSR.scala 369:27]
  reg [39:0] reg_mtval; // @[CSR.scala 370:22]
  reg [63:0] reg_mscratch; // @[CSR.scala 371:25]
  reg [31:0] reg_mtvec; // @[CSR.scala 374:27]
  reg [31:0] reg_mcounteren; // @[CSR.scala 380:18]
  wire [31:0] read_mcounteren = reg_mcounteren & 32'h1f; // @[CSR.scala 381:30]
  reg [31:0] reg_scounteren; // @[CSR.scala 384:18]
  wire [31:0] read_scounteren = reg_scounteren & 32'h1f; // @[CSR.scala 385:36]
  reg [39:0] reg_sepc; // @[CSR.scala 388:21]
  reg [63:0] reg_scause; // @[CSR.scala 389:23]
  reg [39:0] reg_stval; // @[CSR.scala 390:22]
  reg [63:0] reg_sscratch; // @[CSR.scala 391:25]
  reg [38:0] reg_stvec; // @[CSR.scala 392:22]
  reg [3:0] reg_satp_mode; // @[CSR.scala 393:21]
  reg [43:0] reg_satp_ppn; // @[CSR.scala 393:21]
  reg  reg_wfi; // @[CSR.scala 394:50]
  reg [4:0] reg_fflags; // @[CSR.scala 396:23]
  reg [2:0] reg_frm; // @[CSR.scala 397:20]
  reg [5:0] _T_39; // @[Counters.scala 46:37]
  wire [5:0] _GEN_391 = {{5'd0}, io_retire}; // @[Counters.scala 47:33]
  wire [6:0] _T_40 = _T_39 + _GEN_391; // @[Counters.scala 47:33]
  reg [57:0] _T_41; // @[Counters.scala 51:27]
  wire [57:0] _T_44 = _T_41 + 58'h1; // @[Counters.scala 52:43]
  wire [57:0] _GEN_0 = _T_40[6] ? _T_44 : _T_41; // @[Counters.scala 51:27 52:{34,38}]
  wire [63:0] _T_45 = {_T_41,_T_39}; // @[Cat.scala 29:58]
  wire  _T_46 = ~io_csr_stall; // @[CSR.scala 404:103]
  reg [5:0] _T_47; // @[Counters.scala 46:37]
  wire [5:0] _GEN_392 = {{5'd0}, _T_46}; // @[Counters.scala 47:33]
  wire [6:0] _T_48 = _T_47 + _GEN_392; // @[Counters.scala 47:33]
  reg [57:0] _T_49; // @[Counters.scala 51:27]
  wire [57:0] _T_52 = _T_49 + 58'h1; // @[Counters.scala 52:43]
  wire [57:0] _GEN_1 = _T_48[6] ? _T_52 : _T_49; // @[Counters.scala 51:27 52:{34,38}]
  wire [63:0] _T_53 = {_T_49,_T_47}; // @[Cat.scala 29:58]
  reg [63:0] reg_hpmevent_0; // @[CSR.scala 405:46]
  reg [63:0] reg_hpmevent_1; // @[CSR.scala 405:46]
  reg [5:0] _T_54; // @[Counters.scala 46:72]
  wire [5:0] _GEN_393 = {{5'd0}, io_counters_0_inc}; // @[Counters.scala 47:33]
  wire [6:0] _T_55 = _T_54 + _GEN_393; // @[Counters.scala 47:33]
  reg [33:0] _T_56; // @[Counters.scala 51:70]
  wire [33:0] _T_59 = _T_56 + 34'h1; // @[Counters.scala 52:43]
  wire [33:0] _GEN_2 = _T_55[6] ? _T_59 : _T_56; // @[Counters.scala 52:{34,38} 51:70]
  wire [39:0] _T_60 = {_T_56,_T_54}; // @[Cat.scala 29:58]
  reg [5:0] _T_61; // @[Counters.scala 46:72]
  wire [5:0] _GEN_394 = {{5'd0}, io_counters_1_inc}; // @[Counters.scala 47:33]
  wire [6:0] _T_62 = _T_61 + _GEN_394; // @[Counters.scala 47:33]
  reg [33:0] _T_63; // @[Counters.scala 51:70]
  wire [33:0] _T_66 = _T_63 + 34'h1; // @[Counters.scala 52:43]
  wire [33:0] _GEN_3 = _T_62[6] ? _T_66 : _T_63; // @[Counters.scala 52:{34,38} 51:70]
  wire [39:0] _T_67 = {_T_63,_T_61}; // @[Cat.scala 29:58]
  wire  mip_seip = reg_mip_seip | io_interrupts_seip; // @[CSR.scala 415:57]
  wire [7:0] _T_75 = {io_interrupts_mtip,1'h0,reg_mip_stip,1'h0,io_interrupts_msip,1'h0,reg_mip_ssip,1'h0}; // @[CSR.scala 417:22]
  wire [15:0] _T_83 = {2'h0,1'h0,io_rocc_interrupt,io_interrupts_meip,1'h0,mip_seip,1'h0,_T_75}; // @[CSR.scala 417:22]
  wire [15:0] read_mip = _T_83 & 16'haaa; // @[CSR.scala 417:29]
  wire [63:0] _GEN_395 = {{48'd0}, read_mip}; // @[CSR.scala 420:56]
  wire [63:0] pending_interrupts = _GEN_395 & reg_mie; // @[CSR.scala 420:56]
  wire [14:0] d_interrupts = {io_interrupts_debug, 14'h0}; // @[CSR.scala 421:42]
  wire [63:0] _T_87 = ~pending_interrupts; // @[CSR.scala 422:73]
  wire [63:0] _T_88 = _T_87 | read_mideleg; // @[CSR.scala 422:93]
  wire [63:0] _T_89 = ~_T_88; // @[CSR.scala 422:71]
  wire [63:0] m_interrupts = _T_1177 | reg_mstatus_mie ? _T_89 : 64'h0; // @[CSR.scala 422:25]
  wire [63:0] _T_94 = pending_interrupts & read_mideleg; // @[CSR.scala 423:120]
  wire [63:0] s_interrupts = reg_mstatus_prv < 2'h1 | reg_mstatus_prv == 2'h1 & reg_mstatus_sie ? _T_94 : 64'h0; // @[CSR.scala 423:25]
  wire  _T_147 = d_interrupts[14] | d_interrupts[13] | d_interrupts[12] | d_interrupts[11] | d_interrupts[3] |
    d_interrupts[7] | d_interrupts[9] | d_interrupts[1] | d_interrupts[5] | d_interrupts[8] | d_interrupts[0] |
    d_interrupts[4] | m_interrupts[15] | m_interrupts[14] | m_interrupts[13] | m_interrupts[12]; // @[CSR.scala 1052:90]
  wire  _T_162 = _T_147 | m_interrupts[11] | m_interrupts[3] | m_interrupts[7] | m_interrupts[9] | m_interrupts[1] |
    m_interrupts[5] | m_interrupts[8] | m_interrupts[0] | m_interrupts[4] | s_interrupts[15] | s_interrupts[14] |
    s_interrupts[13] | s_interrupts[12] | s_interrupts[11] | s_interrupts[3]; // @[CSR.scala 1052:90]
  wire  anyInterrupt = _T_162 | s_interrupts[7] | s_interrupts[9] | s_interrupts[1] | s_interrupts[5] | s_interrupts[8]
     | s_interrupts[0] | s_interrupts[4]; // @[CSR.scala 1052:90]
  wire [2:0] _T_207 = s_interrupts[0] ? 3'h0 : 3'h4; // @[Mux.scala 47:69]
  wire [3:0] _T_208 = s_interrupts[8] ? 4'h8 : {{1'd0}, _T_207}; // @[Mux.scala 47:69]
  wire [3:0] _T_209 = s_interrupts[5] ? 4'h5 : _T_208; // @[Mux.scala 47:69]
  wire [3:0] _T_210 = s_interrupts[1] ? 4'h1 : _T_209; // @[Mux.scala 47:69]
  wire [3:0] _T_211 = s_interrupts[9] ? 4'h9 : _T_210; // @[Mux.scala 47:69]
  wire [3:0] _T_212 = s_interrupts[7] ? 4'h7 : _T_211; // @[Mux.scala 47:69]
  wire [3:0] _T_213 = s_interrupts[3] ? 4'h3 : _T_212; // @[Mux.scala 47:69]
  wire [3:0] _T_214 = s_interrupts[11] ? 4'hb : _T_213; // @[Mux.scala 47:69]
  wire [3:0] _T_215 = s_interrupts[12] ? 4'hc : _T_214; // @[Mux.scala 47:69]
  wire [3:0] _T_216 = s_interrupts[13] ? 4'hd : _T_215; // @[Mux.scala 47:69]
  wire [3:0] _T_217 = s_interrupts[14] ? 4'he : _T_216; // @[Mux.scala 47:69]
  wire [3:0] _T_218 = s_interrupts[15] ? 4'hf : _T_217; // @[Mux.scala 47:69]
  wire [3:0] _T_219 = m_interrupts[4] ? 4'h4 : _T_218; // @[Mux.scala 47:69]
  wire [3:0] _T_220 = m_interrupts[0] ? 4'h0 : _T_219; // @[Mux.scala 47:69]
  wire [3:0] _T_221 = m_interrupts[8] ? 4'h8 : _T_220; // @[Mux.scala 47:69]
  wire [3:0] _T_222 = m_interrupts[5] ? 4'h5 : _T_221; // @[Mux.scala 47:69]
  wire [3:0] _T_223 = m_interrupts[1] ? 4'h1 : _T_222; // @[Mux.scala 47:69]
  wire [3:0] _T_224 = m_interrupts[9] ? 4'h9 : _T_223; // @[Mux.scala 47:69]
  wire [3:0] _T_225 = m_interrupts[7] ? 4'h7 : _T_224; // @[Mux.scala 47:69]
  wire [3:0] _T_226 = m_interrupts[3] ? 4'h3 : _T_225; // @[Mux.scala 47:69]
  wire [3:0] _T_227 = m_interrupts[11] ? 4'hb : _T_226; // @[Mux.scala 47:69]
  wire [3:0] _T_228 = m_interrupts[12] ? 4'hc : _T_227; // @[Mux.scala 47:69]
  wire [3:0] _T_229 = m_interrupts[13] ? 4'hd : _T_228; // @[Mux.scala 47:69]
  wire [3:0] _T_230 = m_interrupts[14] ? 4'he : _T_229; // @[Mux.scala 47:69]
  wire [3:0] _T_231 = m_interrupts[15] ? 4'hf : _T_230; // @[Mux.scala 47:69]
  wire [3:0] _T_232 = d_interrupts[4] ? 4'h4 : _T_231; // @[Mux.scala 47:69]
  wire [3:0] _T_233 = d_interrupts[0] ? 4'h0 : _T_232; // @[Mux.scala 47:69]
  wire [3:0] _T_234 = d_interrupts[8] ? 4'h8 : _T_233; // @[Mux.scala 47:69]
  wire [3:0] _T_235 = d_interrupts[5] ? 4'h5 : _T_234; // @[Mux.scala 47:69]
  wire [3:0] _T_236 = d_interrupts[1] ? 4'h1 : _T_235; // @[Mux.scala 47:69]
  wire [3:0] _T_237 = d_interrupts[9] ? 4'h9 : _T_236; // @[Mux.scala 47:69]
  wire [3:0] _T_238 = d_interrupts[7] ? 4'h7 : _T_237; // @[Mux.scala 47:69]
  wire [3:0] _T_239 = d_interrupts[3] ? 4'h3 : _T_238; // @[Mux.scala 47:69]
  wire [3:0] _T_240 = d_interrupts[11] ? 4'hb : _T_239; // @[Mux.scala 47:69]
  wire [3:0] _T_241 = d_interrupts[12] ? 4'hc : _T_240; // @[Mux.scala 47:69]
  wire [3:0] _T_242 = d_interrupts[13] ? 4'hd : _T_241; // @[Mux.scala 47:69]
  wire [3:0] whichInterrupt = d_interrupts[14] ? 4'he : _T_242; // @[Mux.scala 47:69]
  wire [63:0] _GEN_396 = {{60'd0}, whichInterrupt}; // @[CSR.scala 426:43]
  wire  _T_244 = ~io_singleStep; // @[CSR.scala 427:36]
  wire [30:0] _T_252 = {reg_pmp_0_addr,reg_pmp_0_cfg_a[0]}; // @[Cat.scala 29:58]
  wire [30:0] _T_255 = _T_252 + 31'h1; // @[PMP.scala 60:23]
  wire [30:0] _T_256 = ~_T_255; // @[PMP.scala 60:16]
  wire [30:0] _T_257 = _T_252 & _T_256; // @[PMP.scala 60:14]
  wire [32:0] _T_258 = {_T_257,2'h3}; // @[Cat.scala 29:58]
  wire [30:0] _T_261 = {reg_pmp_1_addr,reg_pmp_1_cfg_a[0]}; // @[Cat.scala 29:58]
  wire [30:0] _T_264 = _T_261 + 31'h1; // @[PMP.scala 60:23]
  wire [30:0] _T_265 = ~_T_264; // @[PMP.scala 60:16]
  wire [30:0] _T_266 = _T_261 & _T_265; // @[PMP.scala 60:14]
  wire [32:0] _T_267 = {_T_266,2'h3}; // @[Cat.scala 29:58]
  wire [30:0] _T_270 = {reg_pmp_2_addr,reg_pmp_2_cfg_a[0]}; // @[Cat.scala 29:58]
  wire [30:0] _T_273 = _T_270 + 31'h1; // @[PMP.scala 60:23]
  wire [30:0] _T_274 = ~_T_273; // @[PMP.scala 60:16]
  wire [30:0] _T_275 = _T_270 & _T_274; // @[PMP.scala 60:14]
  wire [32:0] _T_276 = {_T_275,2'h3}; // @[Cat.scala 29:58]
  wire [30:0] _T_279 = {reg_pmp_3_addr,reg_pmp_3_cfg_a[0]}; // @[Cat.scala 29:58]
  wire [30:0] _T_282 = _T_279 + 31'h1; // @[PMP.scala 60:23]
  wire [30:0] _T_283 = ~_T_282; // @[PMP.scala 60:16]
  wire [30:0] _T_284 = _T_279 & _T_283; // @[PMP.scala 60:14]
  wire [32:0] _T_285 = {_T_284,2'h3}; // @[Cat.scala 29:58]
  wire [30:0] _T_288 = {reg_pmp_4_addr,reg_pmp_4_cfg_a[0]}; // @[Cat.scala 29:58]
  wire [30:0] _T_291 = _T_288 + 31'h1; // @[PMP.scala 60:23]
  wire [30:0] _T_292 = ~_T_291; // @[PMP.scala 60:16]
  wire [30:0] _T_293 = _T_288 & _T_292; // @[PMP.scala 60:14]
  wire [32:0] _T_294 = {_T_293,2'h3}; // @[Cat.scala 29:58]
  wire [30:0] _T_297 = {reg_pmp_5_addr,reg_pmp_5_cfg_a[0]}; // @[Cat.scala 29:58]
  wire [30:0] _T_300 = _T_297 + 31'h1; // @[PMP.scala 60:23]
  wire [30:0] _T_301 = ~_T_300; // @[PMP.scala 60:16]
  wire [30:0] _T_302 = _T_297 & _T_301; // @[PMP.scala 60:14]
  wire [32:0] _T_303 = {_T_302,2'h3}; // @[Cat.scala 29:58]
  wire [30:0] _T_306 = {reg_pmp_6_addr,reg_pmp_6_cfg_a[0]}; // @[Cat.scala 29:58]
  wire [30:0] _T_309 = _T_306 + 31'h1; // @[PMP.scala 60:23]
  wire [30:0] _T_310 = ~_T_309; // @[PMP.scala 60:16]
  wire [30:0] _T_311 = _T_306 & _T_310; // @[PMP.scala 60:14]
  wire [32:0] _T_312 = {_T_311,2'h3}; // @[Cat.scala 29:58]
  wire [30:0] _T_315 = {reg_pmp_7_addr,reg_pmp_7_cfg_a[0]}; // @[Cat.scala 29:58]
  wire [30:0] _T_318 = _T_315 + 31'h1; // @[PMP.scala 60:23]
  wire [30:0] _T_319 = ~_T_318; // @[PMP.scala 60:16]
  wire [30:0] _T_320 = _T_315 & _T_319; // @[PMP.scala 60:14]
  wire [32:0] _T_321 = {_T_320,2'h3}; // @[Cat.scala 29:58]
  wire [6:0] _T_327 = {io_status_hpie,io_status_spie,io_status_upie,io_status_mie,io_status_hie,io_status_sie,
    io_status_uie}; // @[CSR.scala 446:38]
  wire [18:0] _T_335 = {io_status_sum,io_status_mprv,io_status_xs,io_status_fs,io_status_mpp,io_status_vs,io_status_spp,
    io_status_mpie,_T_327}; // @[CSR.scala 446:38]
  wire [16:0] _T_342 = {io_status_sxl,io_status_uxl,io_status_sd_rv32,io_status_zero1,io_status_tsr,io_status_tw,
    io_status_tvm,io_status_mxr}; // @[CSR.scala 446:38]
  wire [102:0] _T_351 = {io_status_debug,io_status_cease,io_status_wfi,io_status_isa,io_status_dprv,io_status_prv,
    io_status_sd,io_status_zero2,_T_342,_T_335}; // @[CSR.scala 446:38]
  wire [63:0] read_mstatus = _T_351[63:0]; // @[CSR.scala 446:40]
  wire [7:0] _T_353 = reg_mtvec[0] ? 8'hfe : 8'h2; // @[CSR.scala 1081:39]
  wire [31:0] _T_355 = {{24'd0}, _T_353}; // @[package.scala 131:41]
  wire [31:0] _T_356 = ~_T_355; // @[package.scala 131:37]
  wire [31:0] _T_357 = reg_mtvec & _T_356; // @[package.scala 131:35]
  wire [63:0] read_mtvec = {32'h0,_T_357}; // @[Cat.scala 29:58]
  wire [7:0] _T_359 = reg_stvec[0] ? 8'hfe : 8'h2; // @[CSR.scala 1081:39]
  wire [38:0] _T_361 = {{31'd0}, _T_359}; // @[package.scala 131:41]
  wire [38:0] _T_362 = ~_T_361; // @[package.scala 131:37]
  wire [38:0] _T_363 = reg_stvec & _T_362; // @[package.scala 131:35]
  wire [24:0] _T_366 = _T_363[38] ? 25'h1ffffff : 25'h0; // @[Bitwise.scala 72:12]
  wire [63:0] read_stvec = {_T_366,_T_363}; // @[Cat.scala 29:58]
  wire [39:0] _T_385 = ~reg_mepc; // @[CSR.scala 1080:28]
  wire [39:0] _T_388 = _T_385 | 40'h1; // @[CSR.scala 1080:31]
  wire [39:0] _T_389 = ~_T_388; // @[CSR.scala 1080:26]
  wire [23:0] _T_392 = _T_389[39] ? 24'hffffff : 24'h0; // @[Bitwise.scala 72:12]
  wire [63:0] _T_393 = {_T_392,_T_389}; // @[Cat.scala 29:58]
  wire [23:0] _T_396 = reg_mtval[39] ? 24'hffffff : 24'h0; // @[Bitwise.scala 72:12]
  wire [63:0] _T_397 = {_T_396,reg_mtval}; // @[Cat.scala 29:58]
  wire [11:0] _T_403 = {2'h0,1'h0,reg_dcsr_cause,3'h0,reg_dcsr_step,reg_dcsr_prv}; // @[CSR.scala 466:27]
  wire [31:0] _T_410 = {4'h4,12'h0,reg_dcsr_ebreakm,1'h0,reg_dcsr_ebreaks,reg_dcsr_ebreaku,_T_403}; // @[CSR.scala 466:27]
  wire [39:0] _T_411 = ~reg_dpc; // @[CSR.scala 1080:28]
  wire [39:0] _T_414 = _T_411 | 40'h1; // @[CSR.scala 1080:31]
  wire [39:0] _T_415 = ~_T_414; // @[CSR.scala 1080:26]
  wire [23:0] _T_418 = _T_415[39] ? 24'hffffff : 24'h0; // @[Bitwise.scala 72:12]
  wire [63:0] _T_419 = {_T_418,_T_415}; // @[Cat.scala 29:58]
  wire [7:0] read_fcsr = {reg_frm,reg_fflags}; // @[Cat.scala 29:58]
  wire [63:0] _T_420 = reg_mie & read_mideleg; // @[CSR.scala 522:28]
  wire [63:0] _T_421 = _GEN_395 & read_mideleg; // @[CSR.scala 523:29]
  wire [6:0] _T_429 = {1'h0,io_status_spie,2'h0,1'h0,io_status_sie,1'h0}; // @[CSR.scala 537:57]
  wire [18:0] _T_437 = {io_status_sum,1'h0,io_status_xs,io_status_fs,2'h0,io_status_vs,io_status_spp,1'h0,_T_429}; // @[CSR.scala 537:57]
  wire [16:0] _T_444 = {2'h0,io_status_uxl,io_status_sd_rv32,8'h0,2'h0,1'h0,io_status_mxr}; // @[CSR.scala 537:57]
  wire [102:0] _T_453 = {35'h0,4'h0,io_status_sd,27'h0,_T_444,_T_437}; // @[CSR.scala 537:57]
  wire [23:0] _T_457 = reg_stval[39] ? 24'hffffff : 24'h0; // @[Bitwise.scala 72:12]
  wire [63:0] _T_458 = {_T_457,reg_stval}; // @[Cat.scala 29:58]
  wire [63:0] _T_460 = {reg_satp_mode,16'h0,reg_satp_ppn}; // @[CSR.scala 543:43]
  wire [39:0] _T_461 = ~reg_sepc; // @[CSR.scala 1080:28]
  wire [39:0] _T_464 = _T_461 | 40'h1; // @[CSR.scala 1080:31]
  wire [39:0] _T_465 = ~_T_464; // @[CSR.scala 1080:26]
  wire [23:0] _T_468 = _T_465[39] ? 24'hffffff : 24'h0; // @[Bitwise.scala 72:12]
  wire [63:0] _T_469 = {_T_468,_T_465}; // @[Cat.scala 29:58]
  wire [7:0] _T_475 = {reg_pmp_0_cfg_l,2'h0,reg_pmp_0_cfg_a,reg_pmp_0_cfg_x,reg_pmp_0_cfg_w,reg_pmp_0_cfg_r}; // @[package.scala 36:38]
  wire [7:0] _T_485 = {reg_pmp_2_cfg_l,2'h0,reg_pmp_2_cfg_a,reg_pmp_2_cfg_x,reg_pmp_2_cfg_w,reg_pmp_2_cfg_r}; // @[package.scala 36:38]
  wire [7:0] _T_495 = {reg_pmp_4_cfg_l,2'h0,reg_pmp_4_cfg_a,reg_pmp_4_cfg_x,reg_pmp_4_cfg_w,reg_pmp_4_cfg_r}; // @[package.scala 36:38]
  wire [7:0] _T_505 = {reg_pmp_6_cfg_l,2'h0,reg_pmp_6_cfg_a,reg_pmp_6_cfg_x,reg_pmp_6_cfg_w,reg_pmp_6_cfg_r}; // @[package.scala 36:38]
  wire [15:0] _T_511 = {reg_pmp_1_cfg_l,2'h0,reg_pmp_1_cfg_a,reg_pmp_1_cfg_x,reg_pmp_1_cfg_w,reg_pmp_1_cfg_r,_T_475}; // @[Cat.scala 29:58]
  wire [31:0] _T_513 = {reg_pmp_3_cfg_l,2'h0,reg_pmp_3_cfg_a,reg_pmp_3_cfg_x,reg_pmp_3_cfg_w,reg_pmp_3_cfg_r,_T_485,
    _T_511}; // @[Cat.scala 29:58]
  wire [15:0] _T_514 = {reg_pmp_5_cfg_l,2'h0,reg_pmp_5_cfg_a,reg_pmp_5_cfg_x,reg_pmp_5_cfg_w,reg_pmp_5_cfg_r,_T_495}; // @[Cat.scala 29:58]
  wire [63:0] _T_517 = {reg_pmp_7_cfg_l,2'h0,reg_pmp_7_cfg_a,reg_pmp_7_cfg_x,reg_pmp_7_cfg_w,reg_pmp_7_cfg_r,_T_505,
    _T_514,_T_513}; // @[Cat.scala 29:58]
  reg [63:0] reg_custom_0; // @[CSR.scala 566:43]
  wire  _T_568 = io_rw_addr == 12'h301; // @[CSR.scala 574:73]
  wire  _T_569 = io_rw_addr == 12'h300; // @[CSR.scala 574:73]
  wire  _T_570 = io_rw_addr == 12'h305; // @[CSR.scala 574:73]
  wire  _T_571 = io_rw_addr == 12'h344; // @[CSR.scala 574:73]
  wire  _T_572 = io_rw_addr == 12'h304; // @[CSR.scala 574:73]
  wire  _T_573 = io_rw_addr == 12'h340; // @[CSR.scala 574:73]
  wire  _T_574 = io_rw_addr == 12'h341; // @[CSR.scala 574:73]
  wire  _T_575 = io_rw_addr == 12'h343; // @[CSR.scala 574:73]
  wire  _T_576 = io_rw_addr == 12'h342; // @[CSR.scala 574:73]
  wire  _T_577 = io_rw_addr == 12'hf14; // @[CSR.scala 574:73]
  wire  _T_578 = io_rw_addr == 12'h7b0; // @[CSR.scala 574:73]
  wire  _T_579 = io_rw_addr == 12'h7b1; // @[CSR.scala 574:73]
  wire  _T_580 = io_rw_addr == 12'h7b2; // @[CSR.scala 574:73]
  wire  _T_581 = io_rw_addr == 12'h1; // @[CSR.scala 574:73]
  wire  _T_582 = io_rw_addr == 12'h2; // @[CSR.scala 574:73]
  wire  _T_583 = io_rw_addr == 12'h3; // @[CSR.scala 574:73]
  wire  _T_584 = io_rw_addr == 12'hb00; // @[CSR.scala 574:73]
  wire  _T_585 = io_rw_addr == 12'hb02; // @[CSR.scala 574:73]
  wire  _T_586 = io_rw_addr == 12'h323; // @[CSR.scala 574:73]
  wire  _T_587 = io_rw_addr == 12'hb03; // @[CSR.scala 574:73]
  wire  _T_588 = io_rw_addr == 12'hc03; // @[CSR.scala 574:73]
  wire  _T_589 = io_rw_addr == 12'h324; // @[CSR.scala 574:73]
  wire  _T_590 = io_rw_addr == 12'hb04; // @[CSR.scala 574:73]
  wire  _T_591 = io_rw_addr == 12'hc04; // @[CSR.scala 574:73]
  wire  _T_673 = io_rw_addr == 12'h306; // @[CSR.scala 574:73]
  wire  _T_674 = io_rw_addr == 12'hc00; // @[CSR.scala 574:73]
  wire  _T_675 = io_rw_addr == 12'hc02; // @[CSR.scala 574:73]
  wire  _T_676 = io_rw_addr == 12'h100; // @[CSR.scala 574:73]
  wire  _T_677 = io_rw_addr == 12'h144; // @[CSR.scala 574:73]
  wire  _T_678 = io_rw_addr == 12'h104; // @[CSR.scala 574:73]
  wire  _T_679 = io_rw_addr == 12'h140; // @[CSR.scala 574:73]
  wire  _T_680 = io_rw_addr == 12'h142; // @[CSR.scala 574:73]
  wire  _T_681 = io_rw_addr == 12'h143; // @[CSR.scala 574:73]
  wire  _T_682 = io_rw_addr == 12'h180; // @[CSR.scala 574:73]
  wire  _T_683 = io_rw_addr == 12'h141; // @[CSR.scala 574:73]
  wire  _T_684 = io_rw_addr == 12'h105; // @[CSR.scala 574:73]
  wire  _T_685 = io_rw_addr == 12'h106; // @[CSR.scala 574:73]
  wire  _T_686 = io_rw_addr == 12'h303; // @[CSR.scala 574:73]
  wire  _T_687 = io_rw_addr == 12'h302; // @[CSR.scala 574:73]
  wire  _T_688 = io_rw_addr == 12'h3a0; // @[CSR.scala 574:73]
  wire  _T_690 = io_rw_addr == 12'h3b0; // @[CSR.scala 574:73]
  wire  _T_691 = io_rw_addr == 12'h3b1; // @[CSR.scala 574:73]
  wire  _T_692 = io_rw_addr == 12'h3b2; // @[CSR.scala 574:73]
  wire  _T_693 = io_rw_addr == 12'h3b3; // @[CSR.scala 574:73]
  wire  _T_694 = io_rw_addr == 12'h3b4; // @[CSR.scala 574:73]
  wire  _T_695 = io_rw_addr == 12'h3b5; // @[CSR.scala 574:73]
  wire  _T_696 = io_rw_addr == 12'h3b6; // @[CSR.scala 574:73]
  wire  _T_697 = io_rw_addr == 12'h3b7; // @[CSR.scala 574:73]
  wire  _T_706 = io_rw_addr == 12'h7c1; // @[CSR.scala 574:73]
  wire [63:0] _T_711 = io_rw_cmd[1] ? io_rw_rdata : 64'h0; // @[CSR.scala 1058:9]
  wire [63:0] _T_712 = _T_711 | io_rw_wdata; // @[CSR.scala 1058:34]
  wire [63:0] _T_715 = &io_rw_cmd[1:0] ? io_rw_wdata : 64'h0; // @[CSR.scala 1058:49]
  wire [63:0] _T_716 = ~_T_715; // @[CSR.scala 1058:45]
  wire [63:0] wdata = _T_712 & _T_716; // @[CSR.scala 1058:43]
  wire [31:0] _T_730 = _T_717 & 32'h20200000; // @[Decode.scala 14:65]
  wire  _T_731 = _T_730 == 32'h20000000; // @[Decode.scala 14:121]
  wire [31:0] _T_733 = _T_717 & 32'h32200000; // @[Decode.scala 14:65]
  wire  _T_734 = _T_733 == 32'h10000000; // @[Decode.scala 14:121]
  wire  insn_cease = system_insn & _T_731; // @[CSR.scala 589:95]
  wire  insn_wfi = system_insn & _T_734; // @[CSR.scala 589:95]
  wire [31:0] _T_745 = {io_decode_0_csr, 20'h0}; // @[CSR.scala 596:30]
  wire [31:0] _T_752 = _T_745 & 32'h12400000; // @[Decode.scala 14:65]
  wire  _T_753 = _T_752 == 32'h10000000; // @[Decode.scala 14:121]
  wire [31:0] _T_754 = _T_745 & 32'h40000000; // @[Decode.scala 14:65]
  wire  _T_755 = _T_754 == 32'h40000000; // @[Decode.scala 14:121]
  wire  _T_757 = _T_753 | _T_755; // @[Decode.scala 15:30]
  wire [31:0] _T_761 = _T_745 & 32'h32200000; // @[Decode.scala 14:65]
  wire  _T_762 = _T_761 == 32'h10000000; // @[Decode.scala 14:121]
  wire [31:0] _T_764 = _T_745 & 32'h42000000; // @[Decode.scala 14:65]
  wire  _T_765 = _T_764 == 32'h2000000; // @[Decode.scala 14:121]
  wire  _T_773 = reg_mstatus_prv > 2'h1; // @[CSR.scala 598:63]
  wire  _T_776 = reg_mstatus_prv > 2'h1 | ~reg_mstatus_tw; // @[CSR.scala 598:71]
  wire  _T_780 = _T_773 | ~reg_mstatus_tvm; // @[CSR.scala 599:70]
  wire  _T_784 = _T_773 | ~reg_mstatus_tsr; // @[CSR.scala 600:72]
  wire [31:0] _T_787 = read_mcounteren >> io_decode_0_csr[4:0]; // @[CSR.scala 602:68]
  wire [31:0] _T_792 = read_scounteren >> io_decode_0_csr[4:0]; // @[CSR.scala 603:71]
  wire  _T_794 = reg_mstatus_prv >= 2'h1 | _T_792[0]; // @[CSR.scala 603:53]
  wire  _T_795 = (_T_773 | _T_787[0]) & _T_794; // @[CSR.scala 602:84]
  wire [11:0] _T_804 = io_decode_0_csr & 12'h900; // @[Decode.scala 14:65]
  wire  _T_813 = reg_mstatus_prv < io_decode_0_csr[9:8]; // @[CSR.scala 608:44]
  wire  _T_829 = io_decode_0_csr == 12'h7b2; // @[CSR.scala 592:99]
  wire  _T_931 = io_decode_0_csr == 12'h180; // @[CSR.scala 592:99]
  wire  _T_973 = io_decode_0_csr == 12'h7a0 | io_decode_0_csr == 12'h7a1 | io_decode_0_csr == 12'h7a2 | io_decode_0_csr
     == 12'h301 | io_decode_0_csr == 12'h300 | io_decode_0_csr == 12'h305 | io_decode_0_csr == 12'h344 | io_decode_0_csr
     == 12'h304 | io_decode_0_csr == 12'h340 | io_decode_0_csr == 12'h341 | io_decode_0_csr == 12'h343 | io_decode_0_csr
     == 12'h342 | io_decode_0_csr == 12'hf14 | io_decode_0_csr == 12'h7b0 | io_decode_0_csr == 12'h7b1 | _T_829; // @[CSR.scala 592:115]
  wire  _T_988 = _T_973 | io_decode_0_csr == 12'h1 | io_decode_0_csr == 12'h2 | io_decode_0_csr == 12'h3 |
    io_decode_0_csr == 12'hb00 | io_decode_0_csr == 12'hb02 | io_decode_0_csr == 12'h323 | io_decode_0_csr == 12'hb03 |
    io_decode_0_csr == 12'hc03 | io_decode_0_csr == 12'h324 | io_decode_0_csr == 12'hb04 | io_decode_0_csr == 12'hc04 |
    io_decode_0_csr == 12'h325 | io_decode_0_csr == 12'hb05 | io_decode_0_csr == 12'hc05 | io_decode_0_csr == 12'h326; // @[CSR.scala 592:115]
  wire  _T_1003 = _T_988 | io_decode_0_csr == 12'hb06 | io_decode_0_csr == 12'hc06 | io_decode_0_csr == 12'h327 |
    io_decode_0_csr == 12'hb07 | io_decode_0_csr == 12'hc07 | io_decode_0_csr == 12'h328 | io_decode_0_csr == 12'hb08 |
    io_decode_0_csr == 12'hc08 | io_decode_0_csr == 12'h329 | io_decode_0_csr == 12'hb09 | io_decode_0_csr == 12'hc09 |
    io_decode_0_csr == 12'h32a | io_decode_0_csr == 12'hb0a | io_decode_0_csr == 12'hc0a | io_decode_0_csr == 12'h32b; // @[CSR.scala 592:115]
  wire  _T_1018 = _T_1003 | io_decode_0_csr == 12'hb0b | io_decode_0_csr == 12'hc0b | io_decode_0_csr == 12'h32c |
    io_decode_0_csr == 12'hb0c | io_decode_0_csr == 12'hc0c | io_decode_0_csr == 12'h32d | io_decode_0_csr == 12'hb0d |
    io_decode_0_csr == 12'hc0d | io_decode_0_csr == 12'h32e | io_decode_0_csr == 12'hb0e | io_decode_0_csr == 12'hc0e |
    io_decode_0_csr == 12'h32f | io_decode_0_csr == 12'hb0f | io_decode_0_csr == 12'hc0f | io_decode_0_csr == 12'h330; // @[CSR.scala 592:115]
  wire  _T_1033 = _T_1018 | io_decode_0_csr == 12'hb10 | io_decode_0_csr == 12'hc10 | io_decode_0_csr == 12'h331 |
    io_decode_0_csr == 12'hb11 | io_decode_0_csr == 12'hc11 | io_decode_0_csr == 12'h332 | io_decode_0_csr == 12'hb12 |
    io_decode_0_csr == 12'hc12 | io_decode_0_csr == 12'h333 | io_decode_0_csr == 12'hb13 | io_decode_0_csr == 12'hc13 |
    io_decode_0_csr == 12'h334 | io_decode_0_csr == 12'hb14 | io_decode_0_csr == 12'hc14 | io_decode_0_csr == 12'h335; // @[CSR.scala 592:115]
  wire  _T_1048 = _T_1033 | io_decode_0_csr == 12'hb15 | io_decode_0_csr == 12'hc15 | io_decode_0_csr == 12'h336 |
    io_decode_0_csr == 12'hb16 | io_decode_0_csr == 12'hc16 | io_decode_0_csr == 12'h337 | io_decode_0_csr == 12'hb17 |
    io_decode_0_csr == 12'hc17 | io_decode_0_csr == 12'h338 | io_decode_0_csr == 12'hb18 | io_decode_0_csr == 12'hc18 |
    io_decode_0_csr == 12'h339 | io_decode_0_csr == 12'hb19 | io_decode_0_csr == 12'hc19 | io_decode_0_csr == 12'h33a; // @[CSR.scala 592:115]
  wire  _T_1063 = _T_1048 | io_decode_0_csr == 12'hb1a | io_decode_0_csr == 12'hc1a | io_decode_0_csr == 12'h33b |
    io_decode_0_csr == 12'hb1b | io_decode_0_csr == 12'hc1b | io_decode_0_csr == 12'h33c | io_decode_0_csr == 12'hb1c |
    io_decode_0_csr == 12'hc1c | io_decode_0_csr == 12'h33d | io_decode_0_csr == 12'hb1d | io_decode_0_csr == 12'hc1d |
    io_decode_0_csr == 12'h33e | io_decode_0_csr == 12'hb1e | io_decode_0_csr == 12'hc1e | io_decode_0_csr == 12'h33f; // @[CSR.scala 592:115]
  wire  _T_1078 = _T_1063 | io_decode_0_csr == 12'hb1f | io_decode_0_csr == 12'hc1f | io_decode_0_csr == 12'h306 |
    io_decode_0_csr == 12'hc00 | io_decode_0_csr == 12'hc02 | io_decode_0_csr == 12'h100 | io_decode_0_csr == 12'h144 |
    io_decode_0_csr == 12'h104 | io_decode_0_csr == 12'h140 | io_decode_0_csr == 12'h142 | io_decode_0_csr == 12'h143 |
    io_decode_0_csr == 12'h180 | io_decode_0_csr == 12'h141 | io_decode_0_csr == 12'h105 | io_decode_0_csr == 12'h106; // @[CSR.scala 592:115]
  wire  _T_1093 = _T_1078 | io_decode_0_csr == 12'h303 | io_decode_0_csr == 12'h302 | io_decode_0_csr == 12'h3a0 |
    io_decode_0_csr == 12'h3a2 | io_decode_0_csr == 12'h3b0 | io_decode_0_csr == 12'h3b1 | io_decode_0_csr == 12'h3b2 |
    io_decode_0_csr == 12'h3b3 | io_decode_0_csr == 12'h3b4 | io_decode_0_csr == 12'h3b5 | io_decode_0_csr == 12'h3b6 |
    io_decode_0_csr == 12'h3b7 | io_decode_0_csr == 12'h3b8 | io_decode_0_csr == 12'h3b9 | io_decode_0_csr == 12'h3ba; // @[CSR.scala 592:115]
  wire  _T_1102 = _T_1093 | io_decode_0_csr == 12'h3bb | io_decode_0_csr == 12'h3bc | io_decode_0_csr == 12'h3bd |
    io_decode_0_csr == 12'h3be | io_decode_0_csr == 12'h3bf | io_decode_0_csr == 12'h7c1 | io_decode_0_csr == 12'hf13 |
    io_decode_0_csr == 12'hf12 | io_decode_0_csr == 12'hf11; // @[CSR.scala 592:115]
  wire  _T_1103 = ~_T_1102; // @[CSR.scala 609:7]
  wire  _T_1104 = reg_mstatus_prv < io_decode_0_csr[9:8] | _T_1103; // @[CSR.scala 608:62]
  wire  _T_1106 = ~_T_780; // @[CSR.scala 610:35]
  wire  _T_1107 = _T_931 & ~_T_780; // @[CSR.scala 610:32]
  wire  _T_1108 = _T_1104 | _T_1107; // @[CSR.scala 609:32]
  wire  _T_1111 = io_decode_0_csr >= 12'hc00 & io_decode_0_csr < 12'hc20; // @[package.scala 162:55]
  wire  _T_1114 = io_decode_0_csr >= 12'hc80 & io_decode_0_csr < 12'hca0; // @[package.scala 162:55]
  wire  _T_1117 = (_T_1111 | _T_1114) & ~_T_795; // @[CSR.scala 611:130]
  wire  _T_1118 = _T_1108 | _T_1117; // @[CSR.scala 610:53]
  wire [11:0] _T_1119 = io_decode_0_csr & 12'hc10; // @[Decode.scala 14:65]
  wire  _T_1120 = _T_1119 == 12'h410; // @[Decode.scala 14:121]
  wire  _T_1124 = _T_1120 & _T_1246; // @[CSR.scala 612:42]
  wire  _T_1125 = _T_1118 | _T_1124; // @[CSR.scala 611:148]
  wire  _T_1128 = io_decode_0_fp_csr & io_decode_0_fp_illegal; // @[CSR.scala 614:21]
  wire  _T_1143 = _T_762 & ~_T_776; // @[CSR.scala 618:14]
  wire  _T_1144 = _T_813 | _T_1143; // @[CSR.scala 617:64]
  wire  _T_1146 = _T_757 & ~_T_784; // @[CSR.scala 619:14]
  wire  _T_1147 = _T_1144 | _T_1146; // @[CSR.scala 618:28]
  wire  _T_1151 = _T_757 & io_decode_0_csr[10] & _T_1246; // @[CSR.scala 620:32]
  wire  _T_1152 = _T_1147 | _T_1151; // @[CSR.scala 619:29]
  wire  _T_1154 = _T_765 & _T_1106; // @[CSR.scala 621:17]
  wire [11:0] _T_1176 = insn_break ? 12'h800 : 12'h808; // @[CSR.scala 632:37]
  wire [11:0] debugTVec = reg_debug ? _T_1176 : 12'h800; // @[CSR.scala 632:22]
  wire [63:0] _T_1185 = delegate ? read_stvec : read_mtvec; // @[CSR.scala 640:19]
  wire [7:0] _T_1187 = {cause[5:0], 2'h0}; // @[CSR.scala 641:59]
  wire [63:0] _T_1189 = {_T_1185[63:8],_T_1187}; // @[Cat.scala 29:58]
  wire  _T_1195 = _T_1185[0] & cause[63] & cause_lsbs[7:6] == 2'h0; // @[CSR.scala 643:55]
  wire [63:0] _T_1197 = {_T_1185[63:2], 2'h0}; // @[CSR.scala 644:56]
  wire [63:0] notDebugTVec = _T_1195 ? _T_1189 : _T_1197; // @[CSR.scala 644:8]
  wire [63:0] tvec = trapToDebug ? {{52'd0}, debugTVec} : notDebugTVec; // @[CSR.scala 646:17]
  reg [1:0] _T_1210; // @[CSR.scala 657:24]
  wire [1:0] _T_1212 = insn_ret + insn_call; // @[Bitwise.scala 47:55]
  wire [1:0] _T_1214 = insn_break + io_exception; // @[Bitwise.scala 47:55]
  wire [2:0] _T_1216 = _T_1212 + _T_1214; // @[Bitwise.scala 47:55]
  wire  _GEN_36 = insn_wfi & _T_244 & _T_1246 | reg_wfi; // @[CSR.scala 394:50 664:{51,61}]
  wire  _GEN_38 = io_retire | exception | reg_singleStepped; // @[CSR.scala 352:30 667:{36,56}]
  wire [39:0] _T_1244 = ~io_pc; // @[CSR.scala 1079:28]
  wire [39:0] _T_1245 = _T_1244 | 40'h1; // @[CSR.scala 1079:31]
  wire [39:0] epc = ~_T_1245; // @[CSR.scala 1079:26]
  wire [1:0] _T_1247 = causeIsDebugTrigger ? 2'h2 : 2'h1; // @[CSR.scala 681:86]
  wire [1:0] _T_1248 = causeIsDebugInt ? 2'h3 : _T_1247; // @[CSR.scala 681:56]
  wire [2:0] _T_1249 = reg_singleStepped ? 3'h4 : {{1'd0}, _T_1248}; // @[CSR.scala 681:30]
  wire  _GEN_40 = ~reg_debug | reg_debug; // @[CSR.scala 678:25 679:19 349:22]
  wire [39:0] _GEN_41 = ~reg_debug ? epc : reg_dpc; // @[CSR.scala 678:25 680:17 350:20]
  wire [1:0] _GEN_43 = ~reg_debug ? reg_mstatus_prv : reg_dcsr_prv; // @[CSR.scala 308:21 678:25 682:22]
  wire [39:0] _GEN_45 = delegate ? epc : reg_sepc; // @[CSR.scala 685:27 686:16 388:21]
  wire [63:0] _GEN_46 = delegate ? cause : reg_scause; // @[CSR.scala 685:27 687:18 389:23]
  wire [39:0] _GEN_48 = delegate ? io_tval : reg_stval; // @[CSR.scala 685:27 689:17 390:22]
  wire  _GEN_49 = delegate ? reg_mstatus_sie : reg_mstatus_spie; // @[CSR.scala 300:24 685:27 690:24]
  wire [1:0] _GEN_50 = delegate ? reg_mstatus_prv : {{1'd0}, reg_mstatus_spp}; // @[CSR.scala 685:27 691:23 300:24]
  wire  _GEN_51 = delegate ? 1'h0 : reg_mstatus_sie; // @[CSR.scala 685:27 692:23 300:24]
  wire [39:0] _GEN_53 = delegate ? reg_mepc : epc; // @[CSR.scala 368:21 685:27 695:16]
  wire [63:0] _GEN_54 = delegate ? reg_mcause : cause; // @[CSR.scala 369:27 685:27 696:18]
  wire [39:0] _GEN_55 = delegate ? reg_mtval : io_tval; // @[CSR.scala 370:22 685:27 698:17]
  wire  _GEN_56 = delegate ? reg_mstatus_mpie : reg_mstatus_mie; // @[CSR.scala 300:24 685:27 699:24]
  wire [1:0] _GEN_57 = delegate ? reg_mstatus_mpp : reg_mstatus_prv; // @[CSR.scala 300:24 685:27 700:23]
  wire  _GEN_58 = delegate & reg_mstatus_mie; // @[CSR.scala 300:24 685:27 701:23]
  wire  _GEN_59 = trapToDebug ? _GEN_40 : reg_debug; // @[CSR.scala 349:22 677:24]
  wire [39:0] _GEN_60 = trapToDebug ? _GEN_41 : reg_dpc; // @[CSR.scala 350:20 677:24]
  wire [1:0] _GEN_62 = trapToDebug ? _GEN_43 : reg_dcsr_prv; // @[CSR.scala 308:21 677:24]
  wire [39:0] _GEN_64 = trapToDebug ? reg_sepc : _GEN_45; // @[CSR.scala 388:21 677:24]
  wire [63:0] _GEN_65 = trapToDebug ? reg_scause : _GEN_46; // @[CSR.scala 389:23 677:24]
  wire [39:0] _GEN_67 = trapToDebug ? reg_stval : _GEN_48; // @[CSR.scala 390:22 677:24]
  wire  _GEN_68 = trapToDebug ? reg_mstatus_spie : _GEN_49; // @[CSR.scala 300:24 677:24]
  wire [1:0] _GEN_69 = trapToDebug ? {{1'd0}, reg_mstatus_spp} : _GEN_50; // @[CSR.scala 300:24 677:24]
  wire  _GEN_70 = trapToDebug ? reg_mstatus_sie : _GEN_51; // @[CSR.scala 300:24 677:24]
  wire [39:0] _GEN_71 = trapToDebug ? reg_mepc : _GEN_53; // @[CSR.scala 368:21 677:24]
  wire [63:0] _GEN_72 = trapToDebug ? reg_mcause : _GEN_54; // @[CSR.scala 677:24 369:27]
  wire [39:0] _GEN_73 = trapToDebug ? reg_mtval : _GEN_55; // @[CSR.scala 370:22 677:24]
  wire  _GEN_74 = trapToDebug ? reg_mstatus_mpie : _GEN_56; // @[CSR.scala 300:24 677:24]
  wire [1:0] _GEN_75 = trapToDebug ? reg_mstatus_mpp : _GEN_57; // @[CSR.scala 300:24 677:24]
  wire  _GEN_76 = trapToDebug ? reg_mstatus_mie : _GEN_58; // @[CSR.scala 300:24 677:24]
  wire  _GEN_77 = exception ? _GEN_59 : reg_debug; // @[CSR.scala 676:20 349:22]
  wire [39:0] _GEN_78 = exception ? _GEN_60 : reg_dpc; // @[CSR.scala 350:20 676:20]
  wire [1:0] _GEN_80 = exception ? _GEN_62 : reg_dcsr_prv; // @[CSR.scala 676:20 308:21]
  wire [39:0] _GEN_82 = exception ? _GEN_64 : reg_sepc; // @[CSR.scala 676:20 388:21]
  wire [63:0] _GEN_83 = exception ? _GEN_65 : reg_scause; // @[CSR.scala 676:20 389:23]
  wire [39:0] _GEN_85 = exception ? _GEN_67 : reg_stval; // @[CSR.scala 676:20 390:22]
  wire  _GEN_86 = exception ? _GEN_68 : reg_mstatus_spie; // @[CSR.scala 676:20 300:24]
  wire [1:0] _GEN_87 = exception ? _GEN_69 : {{1'd0}, reg_mstatus_spp}; // @[CSR.scala 676:20 300:24]
  wire  _GEN_88 = exception ? _GEN_70 : reg_mstatus_sie; // @[CSR.scala 676:20 300:24]
  wire [39:0] _GEN_89 = exception ? _GEN_71 : reg_mepc; // @[CSR.scala 676:20 368:21]
  wire [63:0] _GEN_90 = exception ? _GEN_72 : reg_mcause; // @[CSR.scala 676:20 369:27]
  wire [39:0] _GEN_91 = exception ? _GEN_73 : reg_mtval; // @[CSR.scala 676:20 370:22]
  wire  _GEN_92 = exception ? _GEN_74 : reg_mstatus_mpie; // @[CSR.scala 676:20 300:24]
  wire [1:0] _GEN_93 = exception ? _GEN_75 : reg_mstatus_mpp; // @[CSR.scala 676:20 300:24]
  wire  _GEN_94 = exception ? _GEN_76 : reg_mstatus_mie; // @[CSR.scala 676:20 300:24]
  wire [39:0] _GEN_97 = io_rw_addr[10] ? _T_415 : _T_389; // @[CSR.scala 733:53 736:15 742:15]
  wire  _GEN_98 = io_rw_addr[10] ? _GEN_94 : reg_mstatus_mpie; // @[CSR.scala 733:53 738:23]
  wire  _GEN_99 = io_rw_addr[10] ? _GEN_92 : 1'h1; // @[CSR.scala 733:53 739:24]
  wire [1:0] _GEN_100 = io_rw_addr[10] ? _GEN_93 : 2'h0; // @[CSR.scala 733:53 740:23]
  wire  _GEN_101 = ~io_rw_addr[9] ? reg_mstatus_spie : _GEN_88; // @[CSR.scala 727:52 728:23]
  wire  _GEN_102 = ~io_rw_addr[9] | _GEN_86; // @[CSR.scala 727:52 729:24]
  wire [1:0] _GEN_103 = ~io_rw_addr[9] ? 2'h0 : _GEN_87; // @[CSR.scala 727:52 730:23]
  wire [39:0] _GEN_105 = ~io_rw_addr[9] ? _T_465 : _GEN_97; // @[CSR.scala 727:52 732:15]
  wire  _GEN_107 = ~io_rw_addr[9] ? _GEN_94 : _GEN_98; // @[CSR.scala 727:52]
  wire  _GEN_108 = ~io_rw_addr[9] ? _GEN_92 : _GEN_99; // @[CSR.scala 727:52]
  wire [1:0] _GEN_109 = ~io_rw_addr[9] ? _GEN_93 : _GEN_100; // @[CSR.scala 727:52]
  wire  _GEN_110 = insn_ret ? _GEN_101 : _GEN_88; // @[CSR.scala 726:19]
  wire  _GEN_111 = insn_ret ? _GEN_102 : _GEN_86; // @[CSR.scala 726:19]
  wire [1:0] _GEN_112 = insn_ret ? _GEN_103 : _GEN_87; // @[CSR.scala 726:19]
  wire [63:0] _GEN_114 = insn_ret ? {{24'd0}, _GEN_105} : tvec; // @[CSR.scala 647:11 726:19]
  wire  _GEN_116 = insn_ret ? _GEN_107 : _GEN_94; // @[CSR.scala 726:19]
  wire  _GEN_117 = insn_ret ? _GEN_108 : _GEN_92; // @[CSR.scala 726:19]
  wire [1:0] _GEN_118 = insn_ret ? _GEN_109 : _GEN_93; // @[CSR.scala 726:19]
  reg  _T_1585; // @[Reg.scala 27:20]
  wire  _GEN_119 = insn_cease | _T_1585; // @[Reg.scala 28:19 27:20 28:23]
  wire [63:0] _T_1589 = _T_568 ? 64'h800000000094112d : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1590 = _T_569 ? read_mstatus : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1591 = _T_570 ? read_mtvec : 64'h0; // @[Mux.scala 27:72]
  wire [15:0] _T_1592 = _T_571 ? read_mip : 16'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1593 = _T_572 ? reg_mie : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1594 = _T_573 ? reg_mscratch : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1595 = _T_574 ? _T_393 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1596 = _T_575 ? _T_397 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1597 = _T_576 ? reg_mcause : 64'h0; // @[Mux.scala 27:72]
  wire  _T_1598 = _T_577 & io_hartid; // @[Mux.scala 27:72]
  wire [31:0] _T_1599 = _T_578 ? _T_410 : 32'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1600 = _T_579 ? _T_419 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1601 = _T_580 ? reg_dscratch : 64'h0; // @[Mux.scala 27:72]
  wire [4:0] _T_1602 = _T_581 ? reg_fflags : 5'h0; // @[Mux.scala 27:72]
  wire [2:0] _T_1603 = _T_582 ? reg_frm : 3'h0; // @[Mux.scala 27:72]
  wire [7:0] _T_1604 = _T_583 ? read_fcsr : 8'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1605 = _T_584 ? _T_53 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1606 = _T_585 ? _T_45 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1607 = _T_586 ? reg_hpmevent_0 : 64'h0; // @[Mux.scala 27:72]
  wire [39:0] _T_1608 = _T_587 ? _T_60 : 40'h0; // @[Mux.scala 27:72]
  wire [39:0] _T_1609 = _T_588 ? _T_60 : 40'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1610 = _T_589 ? reg_hpmevent_1 : 64'h0; // @[Mux.scala 27:72]
  wire [39:0] _T_1611 = _T_590 ? _T_67 : 40'h0; // @[Mux.scala 27:72]
  wire [39:0] _T_1612 = _T_591 ? _T_67 : 40'h0; // @[Mux.scala 27:72]
  wire [31:0] _T_1694 = _T_673 ? read_mcounteren : 32'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1695 = _T_674 ? _T_53 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1696 = _T_675 ? _T_45 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1697 = _T_676 ? _T_453[63:0] : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1698 = _T_677 ? _T_421 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1699 = _T_678 ? _T_420 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1700 = _T_679 ? reg_sscratch : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1701 = _T_680 ? reg_scause : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1702 = _T_681 ? _T_458 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1703 = _T_682 ? _T_460 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1704 = _T_683 ? _T_469 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1705 = _T_684 ? read_stvec : 64'h0; // @[Mux.scala 27:72]
  wire [31:0] _T_1706 = _T_685 ? read_scounteren : 32'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1707 = _T_686 ? read_mideleg : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1708 = _T_687 ? read_medeleg : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1709 = _T_688 ? _T_517 : 64'h0; // @[Mux.scala 27:72]
  wire [29:0] _T_1711 = _T_690 ? reg_pmp_0_addr : 30'h0; // @[Mux.scala 27:72]
  wire [29:0] _T_1712 = _T_691 ? reg_pmp_1_addr : 30'h0; // @[Mux.scala 27:72]
  wire [29:0] _T_1713 = _T_692 ? reg_pmp_2_addr : 30'h0; // @[Mux.scala 27:72]
  wire [29:0] _T_1714 = _T_693 ? reg_pmp_3_addr : 30'h0; // @[Mux.scala 27:72]
  wire [29:0] _T_1715 = _T_694 ? reg_pmp_4_addr : 30'h0; // @[Mux.scala 27:72]
  wire [29:0] _T_1716 = _T_695 ? reg_pmp_5_addr : 30'h0; // @[Mux.scala 27:72]
  wire [29:0] _T_1717 = _T_696 ? reg_pmp_6_addr : 30'h0; // @[Mux.scala 27:72]
  wire [29:0] _T_1718 = _T_697 ? reg_pmp_7_addr : 30'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1727 = _T_706 ? reg_custom_0 : 64'h0; // @[Mux.scala 27:72]
  wire [63:0] _T_1734 = _T_1589 | _T_1590; // @[Mux.scala 27:72]
  wire [63:0] _T_1735 = _T_1734 | _T_1591; // @[Mux.scala 27:72]
  wire [63:0] _GEN_398 = {{48'd0}, _T_1592}; // @[Mux.scala 27:72]
  wire [63:0] _T_1736 = _T_1735 | _GEN_398; // @[Mux.scala 27:72]
  wire [63:0] _T_1737 = _T_1736 | _T_1593; // @[Mux.scala 27:72]
  wire [63:0] _T_1738 = _T_1737 | _T_1594; // @[Mux.scala 27:72]
  wire [63:0] _T_1739 = _T_1738 | _T_1595; // @[Mux.scala 27:72]
  wire [63:0] _T_1740 = _T_1739 | _T_1596; // @[Mux.scala 27:72]
  wire [63:0] _T_1741 = _T_1740 | _T_1597; // @[Mux.scala 27:72]
  wire [63:0] _GEN_399 = {{63'd0}, _T_1598}; // @[Mux.scala 27:72]
  wire [63:0] _T_1742 = _T_1741 | _GEN_399; // @[Mux.scala 27:72]
  wire [63:0] _GEN_400 = {{32'd0}, _T_1599}; // @[Mux.scala 27:72]
  wire [63:0] _T_1743 = _T_1742 | _GEN_400; // @[Mux.scala 27:72]
  wire [63:0] _T_1744 = _T_1743 | _T_1600; // @[Mux.scala 27:72]
  wire [63:0] _T_1745 = _T_1744 | _T_1601; // @[Mux.scala 27:72]
  wire [63:0] _GEN_401 = {{59'd0}, _T_1602}; // @[Mux.scala 27:72]
  wire [63:0] _T_1746 = _T_1745 | _GEN_401; // @[Mux.scala 27:72]
  wire [63:0] _GEN_402 = {{61'd0}, _T_1603}; // @[Mux.scala 27:72]
  wire [63:0] _T_1747 = _T_1746 | _GEN_402; // @[Mux.scala 27:72]
  wire [63:0] _GEN_403 = {{56'd0}, _T_1604}; // @[Mux.scala 27:72]
  wire [63:0] _T_1748 = _T_1747 | _GEN_403; // @[Mux.scala 27:72]
  wire [63:0] _T_1749 = _T_1748 | _T_1605; // @[Mux.scala 27:72]
  wire [63:0] _T_1750 = _T_1749 | _T_1606; // @[Mux.scala 27:72]
  wire [63:0] _T_1751 = _T_1750 | _T_1607; // @[Mux.scala 27:72]
  wire [63:0] _GEN_404 = {{24'd0}, _T_1608}; // @[Mux.scala 27:72]
  wire [63:0] _T_1752 = _T_1751 | _GEN_404; // @[Mux.scala 27:72]
  wire [63:0] _GEN_405 = {{24'd0}, _T_1609}; // @[Mux.scala 27:72]
  wire [63:0] _T_1753 = _T_1752 | _GEN_405; // @[Mux.scala 27:72]
  wire [63:0] _T_1754 = _T_1753 | _T_1610; // @[Mux.scala 27:72]
  wire [63:0] _GEN_406 = {{24'd0}, _T_1611}; // @[Mux.scala 27:72]
  wire [63:0] _T_1755 = _T_1754 | _GEN_406; // @[Mux.scala 27:72]
  wire [63:0] _GEN_407 = {{24'd0}, _T_1612}; // @[Mux.scala 27:72]
  wire [63:0] _T_1756 = _T_1755 | _GEN_407; // @[Mux.scala 27:72]
  wire [63:0] _GEN_408 = {{32'd0}, _T_1694}; // @[Mux.scala 27:72]
  wire [63:0] _T_1838 = _T_1756 | _GEN_408; // @[Mux.scala 27:72]
  wire [63:0] _T_1839 = _T_1838 | _T_1695; // @[Mux.scala 27:72]
  wire [63:0] _T_1840 = _T_1839 | _T_1696; // @[Mux.scala 27:72]
  wire [63:0] _T_1841 = _T_1840 | _T_1697; // @[Mux.scala 27:72]
  wire [63:0] _T_1842 = _T_1841 | _T_1698; // @[Mux.scala 27:72]
  wire [63:0] _T_1843 = _T_1842 | _T_1699; // @[Mux.scala 27:72]
  wire [63:0] _T_1844 = _T_1843 | _T_1700; // @[Mux.scala 27:72]
  wire [63:0] _T_1845 = _T_1844 | _T_1701; // @[Mux.scala 27:72]
  wire [63:0] _T_1846 = _T_1845 | _T_1702; // @[Mux.scala 27:72]
  wire [63:0] _T_1847 = _T_1846 | _T_1703; // @[Mux.scala 27:72]
  wire [63:0] _T_1848 = _T_1847 | _T_1704; // @[Mux.scala 27:72]
  wire [63:0] _T_1849 = _T_1848 | _T_1705; // @[Mux.scala 27:72]
  wire [63:0] _GEN_409 = {{32'd0}, _T_1706}; // @[Mux.scala 27:72]
  wire [63:0] _T_1850 = _T_1849 | _GEN_409; // @[Mux.scala 27:72]
  wire [63:0] _T_1851 = _T_1850 | _T_1707; // @[Mux.scala 27:72]
  wire [63:0] _T_1852 = _T_1851 | _T_1708; // @[Mux.scala 27:72]
  wire [63:0] _T_1853 = _T_1852 | _T_1709; // @[Mux.scala 27:72]
  wire [63:0] _GEN_410 = {{34'd0}, _T_1711}; // @[Mux.scala 27:72]
  wire [63:0] _T_1855 = _T_1853 | _GEN_410; // @[Mux.scala 27:72]
  wire [63:0] _GEN_411 = {{34'd0}, _T_1712}; // @[Mux.scala 27:72]
  wire [63:0] _T_1856 = _T_1855 | _GEN_411; // @[Mux.scala 27:72]
  wire [63:0] _GEN_412 = {{34'd0}, _T_1713}; // @[Mux.scala 27:72]
  wire [63:0] _T_1857 = _T_1856 | _GEN_412; // @[Mux.scala 27:72]
  wire [63:0] _GEN_413 = {{34'd0}, _T_1714}; // @[Mux.scala 27:72]
  wire [63:0] _T_1858 = _T_1857 | _GEN_413; // @[Mux.scala 27:72]
  wire [63:0] _GEN_414 = {{34'd0}, _T_1715}; // @[Mux.scala 27:72]
  wire [63:0] _T_1859 = _T_1858 | _GEN_414; // @[Mux.scala 27:72]
  wire [63:0] _GEN_415 = {{34'd0}, _T_1716}; // @[Mux.scala 27:72]
  wire [63:0] _T_1860 = _T_1859 | _GEN_415; // @[Mux.scala 27:72]
  wire [63:0] _GEN_416 = {{34'd0}, _T_1717}; // @[Mux.scala 27:72]
  wire [63:0] _T_1861 = _T_1860 | _GEN_416; // @[Mux.scala 27:72]
  wire [63:0] _GEN_417 = {{34'd0}, _T_1718}; // @[Mux.scala 27:72]
  wire [63:0] _T_1862 = _T_1861 | _GEN_417; // @[Mux.scala 27:72]
  wire  _T_1878 = io_rw_cmd == 3'h5; // @[package.scala 15:47]
  wire  _T_1879 = io_rw_cmd == 3'h6; // @[package.scala 15:47]
  wire  _T_1880 = io_rw_cmd == 3'h7; // @[package.scala 15:47]
  wire  csr_wen = _T_1879 | _T_1880 | _T_1878; // @[package.scala 64:59]
  wire  _GEN_122 = io_fcsr_flags_valid | io_set_fs_dirty; // @[CSR.scala 785:30 787:18]
  wire  _GEN_156 = _T_581 | _GEN_122; // @[CSR.scala 865:{40,55}]
  wire  _GEN_158 = _T_582 | _GEN_156; // @[CSR.scala 866:{40,55}]
  wire  _GEN_160 = _T_583 | _GEN_158; // @[CSR.scala 867:38 868:22]
  wire  set_fs_dirty = csr_wen ? _GEN_160 : _GEN_122; // @[CSR.scala 799:18]
  wire [1:0] _GEN_120 = set_fs_dirty ? 2'h3 : reg_mstatus_fs; // @[CSR.scala 778:25 780:22 300:24]
  wire [4:0] _T_3620 = reg_fflags | io_fcsr_flags_bits; // @[CSR.scala 786:30]
  wire [4:0] _GEN_121 = io_fcsr_flags_valid ? _T_3620 : reg_fflags; // @[CSR.scala 785:30 786:16 396:23]
  wire  _T_3628 = io_rw_addr >= 12'hb00 & io_rw_addr < 12'hb20; // @[package.scala 162:55]
  wire  _T_3631 = io_rw_addr >= 12'hb80 & io_rw_addr < 12'hba0; // @[package.scala 162:55]
  wire [63:0] _T_3635 = 64'h1 << io_rw_addr[5:0]; // @[OneHot.scala 58:35]
  wire [63:0] _T_3636 = csr_wen & (_T_3628 | _T_3631) ? _T_3635 : 64'h0; // @[CSR.scala 798:25]
  wire [102:0] _T_3638 = {{39'd0}, wdata};
  wire [1:0] _GEN_127 = _T_569 ? {{1'd0}, _T_3638[8]} : _GEN_112; // @[CSR.scala 800:39 809:27]
  wire [15:0] _T_3692 = {4'h0,2'h0,reg_mip_seip,1'h0,2'h0,reg_mip_stip,1'h0,2'h0,reg_mip_ssip,1'h0}; // @[CSR.scala 840:59]
  wire [15:0] _T_3694 = io_rw_cmd[1] ? _T_3692 : 16'h0; // @[CSR.scala 1058:9]
  wire [63:0] _GEN_418 = {{48'd0}, _T_3694}; // @[CSR.scala 1058:34]
  wire [63:0] _T_3695 = _GEN_418 | io_rw_wdata; // @[CSR.scala 1058:34]
  wire [63:0] _T_3700 = _T_3695 & _T_716; // @[CSR.scala 1058:43]
  wire [63:0] _T_3719 = wdata & 64'haaa; // @[CSR.scala 847:59]
  wire [63:0] _T_3720 = ~wdata; // @[CSR.scala 1079:28]
  wire [63:0] _T_3721 = _T_3720 | 64'h1; // @[CSR.scala 1079:31]
  wire [63:0] _T_3722 = ~_T_3721; // @[CSR.scala 1079:26]
  wire [63:0] _GEN_141 = _T_574 ? _T_3722 : {{24'd0}, _GEN_89}; // @[CSR.scala 848:{40,51}]
  wire [63:0] _GEN_143 = _T_570 ? wdata : {{32'd0}, reg_mtvec}; // @[CSR.scala 374:27 851:{40,52}]
  wire [63:0] _T_3723 = wdata & 64'h800000000000000f; // @[CSR.scala 852:62]
  wire [39:0] _GEN_146 = _T_587 ? wdata[39:0] : {{33'd0}, _T_55}; // @[CSR.scala 1076:31 Counters.scala 66:11 48:9]
  wire [63:0] _T_3727 = wdata & 64'h3f03; // @[Events.scala 30:14]
  wire [39:0] _GEN_149 = _T_590 ? wdata[39:0] : {{33'd0}, _T_62}; // @[CSR.scala 1076:31 Counters.scala 66:11 48:9]
  wire [63:0] _GEN_152 = _T_584 ? wdata : {{57'd0}, _T_48}; // @[CSR.scala 1076:31 Counters.scala 66:11 48:9]
  wire [63:0] _GEN_154 = _T_585 ? wdata : {{57'd0}, _T_40}; // @[CSR.scala 1076:31 Counters.scala 66:11 48:9]
  wire [63:0] _GEN_157 = _T_581 ? wdata : {{59'd0}, _GEN_121}; // @[CSR.scala 865:{40,75}]
  wire [63:0] _GEN_159 = _T_582 ? wdata : {{61'd0}, reg_frm}; // @[CSR.scala 397:20 866:{40,72}]
  wire [63:0] _GEN_161 = _T_583 ? wdata : _GEN_157; // @[CSR.scala 867:38 869:20]
  wire [63:0] _GEN_162 = _T_583 ? {{5'd0}, wdata[63:5]} : _GEN_159; // @[CSR.scala 867:38 870:17]
  wire [63:0] _GEN_168 = _T_579 ? _T_3722 : {{24'd0}, _GEN_78}; // @[CSR.scala 882:{42,52}]
  wire [1:0] _GEN_172 = _T_676 ? {{1'd0}, _T_3638[8]} : _GEN_127; // @[CSR.scala 886:41 890:25]
  wire [63:0] _T_3790 = ~read_mideleg; // @[CSR.scala 900:54]
  wire [63:0] _T_3791 = _GEN_395 & _T_3790; // @[CSR.scala 900:52]
  wire [63:0] _T_3792 = wdata & read_mideleg; // @[CSR.scala 900:78]
  wire [63:0] _T_3793 = _T_3791 | _T_3792; // @[CSR.scala 900:69]
  wire  _T_3817 = wdata[63:60] == 4'h0; // @[package.scala 15:47]
  wire  _T_3818 = wdata[63:60] == 4'h8; // @[package.scala 15:47]
  wire  _T_3819 = _T_3817 | _T_3818; // @[package.scala 64:59]
  wire [3:0] _T_3820 = wdata[63:60] & 4'h8; // @[CSR.scala 908:44]
  wire [63:0] _T_3823 = reg_mie & _T_3790; // @[CSR.scala 914:64]
  wire [63:0] _T_3825 = _T_3823 | _T_3792; // @[CSR.scala 914:81]
  wire [63:0] _GEN_184 = _T_683 ? _T_3722 : {{24'd0}, _GEN_82}; // @[CSR.scala 916:{42,53}]
  wire [63:0] _GEN_185 = _T_684 ? wdata : {{25'd0}, reg_stvec}; // @[CSR.scala 392:22 917:{42,54}]
  wire [63:0] _T_3829 = wdata & 64'h800000000000001f; // @[CSR.scala 918:64]
  wire [63:0] _GEN_190 = _T_685 ? wdata : {{32'd0}, reg_scounteren}; // @[CSR.scala 384:18 922:{44,61}]
  wire [63:0] _GEN_191 = _T_673 ? wdata : {{32'd0}, reg_mcounteren}; // @[CSR.scala 380:18 925:{44,61}]
  wire  _T_3846 = ~reg_pmp_1_cfg_a[1] & reg_pmp_1_cfg_a[0]; // @[PMP.scala 49:20]
  wire  _T_3848 = reg_pmp_0_cfg_l | reg_pmp_1_cfg_l & _T_3846; // @[PMP.scala 51:44]
  wire [63:0] _GEN_198 = _T_690 & ~_T_3848 ? wdata : {{34'd0}, reg_pmp_0_addr}; // @[CSR.scala 960:71 961:18 356:20]
  wire  _T_3866 = ~reg_pmp_2_cfg_a[1] & reg_pmp_2_cfg_a[0]; // @[PMP.scala 49:20]
  wire  _T_3868 = reg_pmp_1_cfg_l | reg_pmp_2_cfg_l & _T_3866; // @[PMP.scala 51:44]
  wire [63:0] _GEN_205 = _T_691 & ~_T_3868 ? wdata : {{34'd0}, reg_pmp_1_addr}; // @[CSR.scala 960:71 961:18 356:20]
  wire  _T_3886 = ~reg_pmp_3_cfg_a[1] & reg_pmp_3_cfg_a[0]; // @[PMP.scala 49:20]
  wire  _T_3888 = reg_pmp_2_cfg_l | reg_pmp_3_cfg_l & _T_3886; // @[PMP.scala 51:44]
  wire [63:0] _GEN_212 = _T_692 & ~_T_3888 ? wdata : {{34'd0}, reg_pmp_2_addr}; // @[CSR.scala 960:71 961:18 356:20]
  wire  _T_3906 = ~reg_pmp_4_cfg_a[1] & reg_pmp_4_cfg_a[0]; // @[PMP.scala 49:20]
  wire  _T_3908 = reg_pmp_3_cfg_l | reg_pmp_4_cfg_l & _T_3906; // @[PMP.scala 51:44]
  wire [63:0] _GEN_219 = _T_693 & ~_T_3908 ? wdata : {{34'd0}, reg_pmp_3_addr}; // @[CSR.scala 960:71 961:18 356:20]
  wire  _T_3926 = ~reg_pmp_5_cfg_a[1] & reg_pmp_5_cfg_a[0]; // @[PMP.scala 49:20]
  wire  _T_3928 = reg_pmp_4_cfg_l | reg_pmp_5_cfg_l & _T_3926; // @[PMP.scala 51:44]
  wire [63:0] _GEN_226 = _T_694 & ~_T_3928 ? wdata : {{34'd0}, reg_pmp_4_addr}; // @[CSR.scala 960:71 961:18 356:20]
  wire  _T_3946 = ~reg_pmp_6_cfg_a[1] & reg_pmp_6_cfg_a[0]; // @[PMP.scala 49:20]
  wire  _T_3948 = reg_pmp_5_cfg_l | reg_pmp_6_cfg_l & _T_3946; // @[PMP.scala 51:44]
  wire [63:0] _GEN_233 = _T_695 & ~_T_3948 ? wdata : {{34'd0}, reg_pmp_5_addr}; // @[CSR.scala 960:71 961:18 356:20]
  wire  _T_3966 = ~reg_pmp_7_cfg_a[1] & reg_pmp_7_cfg_a[0]; // @[PMP.scala 49:20]
  wire  _T_3968 = reg_pmp_6_cfg_l | reg_pmp_7_cfg_l & _T_3966; // @[PMP.scala 51:44]
  wire [63:0] _GEN_240 = _T_696 & ~_T_3968 ? wdata : {{34'd0}, reg_pmp_6_addr}; // @[CSR.scala 960:71 961:18 356:20]
  wire  _T_3988 = reg_pmp_7_cfg_l | reg_pmp_7_cfg_l & _T_3966; // @[PMP.scala 51:44]
  wire [63:0] _GEN_247 = _T_697 & ~_T_3988 ? wdata : {{34'd0}, reg_pmp_7_addr}; // @[CSR.scala 960:71 961:18 356:20]
  wire [63:0] _T_3991 = wdata & 64'h8; // @[CSR.scala 967:23]
  wire [63:0] _T_3993 = reg_custom_0 & 64'hfffffffffffffff7; // @[CSR.scala 967:38]
  wire [63:0] _T_3994 = _T_3991 | _T_3993; // @[CSR.scala 967:31]
  wire [1:0] _GEN_254 = csr_wen ? _GEN_172 : _GEN_112; // @[CSR.scala 799:18]
  wire [63:0] _GEN_268 = csr_wen ? _GEN_141 : {{24'd0}, _GEN_89}; // @[CSR.scala 799:18]
  wire [63:0] _GEN_270 = csr_wen ? _GEN_143 : {{32'd0}, reg_mtvec}; // @[CSR.scala 799:18 374:27]
  wire [39:0] _GEN_273 = csr_wen ? _GEN_146 : {{33'd0}, _T_55}; // @[CSR.scala 799:18 Counters.scala 48:9]
  wire [39:0] _GEN_276 = csr_wen ? _GEN_149 : {{33'd0}, _T_62}; // @[CSR.scala 799:18 Counters.scala 48:9]
  wire [63:0] _GEN_279 = csr_wen ? _GEN_152 : {{57'd0}, _T_48}; // @[CSR.scala 799:18 Counters.scala 48:9]
  wire [63:0] _GEN_281 = csr_wen ? _GEN_154 : {{57'd0}, _T_40}; // @[CSR.scala 799:18 Counters.scala 48:9]
  wire [63:0] _GEN_284 = csr_wen ? _GEN_161 : {{59'd0}, _GEN_121}; // @[CSR.scala 799:18]
  wire [63:0] _GEN_285 = csr_wen ? _GEN_162 : {{61'd0}, reg_frm}; // @[CSR.scala 799:18 397:20]
  wire [63:0] _GEN_291 = csr_wen ? _GEN_168 : {{24'd0}, _GEN_78}; // @[CSR.scala 799:18]
  wire [63:0] _GEN_296 = csr_wen ? _GEN_184 : {{24'd0}, _GEN_82}; // @[CSR.scala 799:18]
  wire [63:0] _GEN_297 = csr_wen ? _GEN_185 : {{25'd0}, reg_stvec}; // @[CSR.scala 799:18 392:22]
  wire [63:0] _GEN_302 = csr_wen ? _GEN_190 : {{32'd0}, reg_scounteren}; // @[CSR.scala 384:18 799:18]
  wire [63:0] _GEN_303 = csr_wen ? _GEN_191 : {{32'd0}, reg_mcounteren}; // @[CSR.scala 380:18 799:18]
  wire [63:0] _GEN_310 = csr_wen ? _GEN_198 : {{34'd0}, reg_pmp_0_addr}; // @[CSR.scala 799:18 356:20]
  wire [63:0] _GEN_317 = csr_wen ? _GEN_205 : {{34'd0}, reg_pmp_1_addr}; // @[CSR.scala 799:18 356:20]
  wire [63:0] _GEN_324 = csr_wen ? _GEN_212 : {{34'd0}, reg_pmp_2_addr}; // @[CSR.scala 799:18 356:20]
  wire [63:0] _GEN_331 = csr_wen ? _GEN_219 : {{34'd0}, reg_pmp_3_addr}; // @[CSR.scala 799:18 356:20]
  wire [63:0] _GEN_338 = csr_wen ? _GEN_226 : {{34'd0}, reg_pmp_4_addr}; // @[CSR.scala 799:18 356:20]
  wire [63:0] _GEN_345 = csr_wen ? _GEN_233 : {{34'd0}, reg_pmp_5_addr}; // @[CSR.scala 799:18 356:20]
  wire [63:0] _GEN_352 = csr_wen ? _GEN_240 : {{34'd0}, reg_pmp_6_addr}; // @[CSR.scala 799:18 356:20]
  wire [63:0] _GEN_359 = csr_wen ? _GEN_247 : {{34'd0}, reg_pmp_7_addr}; // @[CSR.scala 799:18 356:20]
  wire [1:0] _GEN_420 = reset ? 2'h0 : _GEN_254; // @[CSR.scala 300:{24,24}]
  wire [63:0] _GEN_421 = reset ? 64'h0 : _GEN_270; // @[CSR.scala 374:{27,27}]
  wire [63:0] _GEN_422 = reset ? 64'h0 : _GEN_281; // @[Counters.scala 46:{37,37}]
  wire [63:0] _GEN_423 = reset ? 64'h0 : _GEN_279; // @[Counters.scala 46:{37,37}]
  assign io_rw_rdata = _T_1862 | _T_1727; // @[Mux.scala 27:72]
  assign io_decode_0_fp_illegal = io_status_fs == 2'h0; // @[CSR.scala 604:39]
  assign io_decode_0_vector_illegal = 1'h1; // @[CSR.scala 605:49]
  assign io_decode_0_fp_csr = _T_804 == 12'h0; // @[Decode.scala 14:121]
  assign io_decode_0_rocc_illegal = io_status_xs == 2'h0; // @[CSR.scala 607:41]
  assign io_decode_0_read_illegal = _T_1125 | _T_1128; // @[CSR.scala 613:68]
  assign io_decode_0_write_illegal = &io_decode_0_csr[11:10]; // @[CSR.scala 615:47]
  assign io_decode_0_write_flush = ~(io_decode_0_csr >= 12'h340 & io_decode_0_csr <= 12'h343 | io_decode_0_csr >= 12'h140
     & io_decode_0_csr <= 12'h143); // @[CSR.scala 616:27]
  assign io_decode_0_system_illegal = _T_1152 | _T_1154; // @[CSR.scala 620:46]
  assign io_csr_stall = reg_wfi | io_status_cease; // @[CSR.scala 747:27]
  assign io_eret = _T_1211 | insn_ret; // @[CSR.scala 649:38]
  assign io_singleStep = reg_dcsr_step & _T_1246; // @[CSR.scala 650:34]
  assign io_status_debug = reg_debug; // @[CSR.scala 653:19]
  assign io_status_cease = _T_1585; // @[CSR.scala 748:19]
  assign io_status_wfi = reg_wfi; // @[CSR.scala 749:17]
  assign io_status_isa = 32'h94112d; // @[CSR.scala 654:17]
  assign io_status_dprv = _T_1210; // @[CSR.scala 657:18]
  assign io_status_prv = reg_mstatus_prv; // @[CSR.scala 651:13]
  assign io_status_sd = &io_status_fs | &io_status_xs | &io_status_vs; // @[CSR.scala 652:58]
  assign io_status_zero2 = 27'h0; // @[CSR.scala 651:13]
  assign io_status_sxl = 2'h2; // @[CSR.scala 656:17]
  assign io_status_uxl = 2'h2; // @[CSR.scala 655:17]
  assign io_status_sd_rv32 = 1'h0; // @[CSR.scala 651:13]
  assign io_status_zero1 = 8'h0; // @[CSR.scala 651:13]
  assign io_status_tsr = reg_mstatus_tsr; // @[CSR.scala 651:13]
  assign io_status_tw = reg_mstatus_tw; // @[CSR.scala 651:13]
  assign io_status_tvm = reg_mstatus_tvm; // @[CSR.scala 651:13]
  assign io_status_mxr = reg_mstatus_mxr; // @[CSR.scala 651:13]
  assign io_status_sum = reg_mstatus_sum; // @[CSR.scala 651:13]
  assign io_status_mprv = reg_mstatus_mprv; // @[CSR.scala 651:13]
  assign io_status_xs = 2'h0; // @[CSR.scala 651:13]
  assign io_status_fs = reg_mstatus_fs; // @[CSR.scala 651:13]
  assign io_status_mpp = reg_mstatus_mpp; // @[CSR.scala 651:13]
  assign io_status_vs = 2'h0; // @[CSR.scala 651:13]
  assign io_status_spp = reg_mstatus_spp; // @[CSR.scala 651:13]
  assign io_status_mpie = reg_mstatus_mpie; // @[CSR.scala 651:13]
  assign io_status_hpie = 1'h0; // @[CSR.scala 651:13]
  assign io_status_spie = reg_mstatus_spie; // @[CSR.scala 651:13]
  assign io_status_upie = 1'h0; // @[CSR.scala 651:13]
  assign io_status_mie = reg_mstatus_mie; // @[CSR.scala 651:13]
  assign io_status_hie = 1'h0; // @[CSR.scala 651:13]
  assign io_status_sie = reg_mstatus_sie; // @[CSR.scala 651:13]
  assign io_status_uie = 1'h0; // @[CSR.scala 651:13]
  assign io_ptbr_mode = reg_satp_mode; // @[CSR.scala 648:11]
  assign io_ptbr_asid = 16'h0; // @[CSR.scala 648:11]
  assign io_ptbr_ppn = reg_satp_ppn; // @[CSR.scala 648:11]
  assign io_evec = _GEN_114[39:0];
  assign io_time = {_T_49,_T_47}; // @[Cat.scala 29:58]
  assign io_fcsr_rm = reg_frm; // @[CSR.scala 784:14]
  assign io_interrupt = (anyInterrupt & ~io_singleStep | reg_singleStepped) & ~(reg_debug | io_status_cease); // @[CSR.scala 427:73]
  assign io_interrupt_cause = 64'h8000000000000000 + _GEN_396; // @[CSR.scala 426:43]
  assign io_pmp_0_cfg_l = reg_pmp_0_cfg_l; // @[PMP.scala 26:19 27:13]
  assign io_pmp_0_cfg_res = 2'h0; // @[PMP.scala 26:19 27:13]
  assign io_pmp_0_cfg_a = reg_pmp_0_cfg_a; // @[PMP.scala 26:19 27:13]
  assign io_pmp_0_cfg_x = reg_pmp_0_cfg_x; // @[PMP.scala 26:19 27:13]
  assign io_pmp_0_cfg_w = reg_pmp_0_cfg_w; // @[PMP.scala 26:19 27:13]
  assign io_pmp_0_cfg_r = reg_pmp_0_cfg_r; // @[PMP.scala 26:19 27:13]
  assign io_pmp_0_addr = reg_pmp_0_addr; // @[PMP.scala 26:19 28:14]
  assign io_pmp_0_mask = _T_258[31:0]; // @[PMP.scala 26:19 29:14]
  assign io_pmp_1_cfg_l = reg_pmp_1_cfg_l; // @[PMP.scala 26:19 27:13]
  assign io_pmp_1_cfg_res = 2'h0; // @[PMP.scala 26:19 27:13]
  assign io_pmp_1_cfg_a = reg_pmp_1_cfg_a; // @[PMP.scala 26:19 27:13]
  assign io_pmp_1_cfg_x = reg_pmp_1_cfg_x; // @[PMP.scala 26:19 27:13]
  assign io_pmp_1_cfg_w = reg_pmp_1_cfg_w; // @[PMP.scala 26:19 27:13]
  assign io_pmp_1_cfg_r = reg_pmp_1_cfg_r; // @[PMP.scala 26:19 27:13]
  assign io_pmp_1_addr = reg_pmp_1_addr; // @[PMP.scala 26:19 28:14]
  assign io_pmp_1_mask = _T_267[31:0]; // @[PMP.scala 26:19 29:14]
  assign io_pmp_2_cfg_l = reg_pmp_2_cfg_l; // @[PMP.scala 26:19 27:13]
  assign io_pmp_2_cfg_res = 2'h0; // @[PMP.scala 26:19 27:13]
  assign io_pmp_2_cfg_a = reg_pmp_2_cfg_a; // @[PMP.scala 26:19 27:13]
  assign io_pmp_2_cfg_x = reg_pmp_2_cfg_x; // @[PMP.scala 26:19 27:13]
  assign io_pmp_2_cfg_w = reg_pmp_2_cfg_w; // @[PMP.scala 26:19 27:13]
  assign io_pmp_2_cfg_r = reg_pmp_2_cfg_r; // @[PMP.scala 26:19 27:13]
  assign io_pmp_2_addr = reg_pmp_2_addr; // @[PMP.scala 26:19 28:14]
  assign io_pmp_2_mask = _T_276[31:0]; // @[PMP.scala 26:19 29:14]
  assign io_pmp_3_cfg_l = reg_pmp_3_cfg_l; // @[PMP.scala 26:19 27:13]
  assign io_pmp_3_cfg_res = 2'h0; // @[PMP.scala 26:19 27:13]
  assign io_pmp_3_cfg_a = reg_pmp_3_cfg_a; // @[PMP.scala 26:19 27:13]
  assign io_pmp_3_cfg_x = reg_pmp_3_cfg_x; // @[PMP.scala 26:19 27:13]
  assign io_pmp_3_cfg_w = reg_pmp_3_cfg_w; // @[PMP.scala 26:19 27:13]
  assign io_pmp_3_cfg_r = reg_pmp_3_cfg_r; // @[PMP.scala 26:19 27:13]
  assign io_pmp_3_addr = reg_pmp_3_addr; // @[PMP.scala 26:19 28:14]
  assign io_pmp_3_mask = _T_285[31:0]; // @[PMP.scala 26:19 29:14]
  assign io_pmp_4_cfg_l = reg_pmp_4_cfg_l; // @[PMP.scala 26:19 27:13]
  assign io_pmp_4_cfg_res = 2'h0; // @[PMP.scala 26:19 27:13]
  assign io_pmp_4_cfg_a = reg_pmp_4_cfg_a; // @[PMP.scala 26:19 27:13]
  assign io_pmp_4_cfg_x = reg_pmp_4_cfg_x; // @[PMP.scala 26:19 27:13]
  assign io_pmp_4_cfg_w = reg_pmp_4_cfg_w; // @[PMP.scala 26:19 27:13]
  assign io_pmp_4_cfg_r = reg_pmp_4_cfg_r; // @[PMP.scala 26:19 27:13]
  assign io_pmp_4_addr = reg_pmp_4_addr; // @[PMP.scala 26:19 28:14]
  assign io_pmp_4_mask = _T_294[31:0]; // @[PMP.scala 26:19 29:14]
  assign io_pmp_5_cfg_l = reg_pmp_5_cfg_l; // @[PMP.scala 26:19 27:13]
  assign io_pmp_5_cfg_res = 2'h0; // @[PMP.scala 26:19 27:13]
  assign io_pmp_5_cfg_a = reg_pmp_5_cfg_a; // @[PMP.scala 26:19 27:13]
  assign io_pmp_5_cfg_x = reg_pmp_5_cfg_x; // @[PMP.scala 26:19 27:13]
  assign io_pmp_5_cfg_w = reg_pmp_5_cfg_w; // @[PMP.scala 26:19 27:13]
  assign io_pmp_5_cfg_r = reg_pmp_5_cfg_r; // @[PMP.scala 26:19 27:13]
  assign io_pmp_5_addr = reg_pmp_5_addr; // @[PMP.scala 26:19 28:14]
  assign io_pmp_5_mask = _T_303[31:0]; // @[PMP.scala 26:19 29:14]
  assign io_pmp_6_cfg_l = reg_pmp_6_cfg_l; // @[PMP.scala 26:19 27:13]
  assign io_pmp_6_cfg_res = 2'h0; // @[PMP.scala 26:19 27:13]
  assign io_pmp_6_cfg_a = reg_pmp_6_cfg_a; // @[PMP.scala 26:19 27:13]
  assign io_pmp_6_cfg_x = reg_pmp_6_cfg_x; // @[PMP.scala 26:19 27:13]
  assign io_pmp_6_cfg_w = reg_pmp_6_cfg_w; // @[PMP.scala 26:19 27:13]
  assign io_pmp_6_cfg_r = reg_pmp_6_cfg_r; // @[PMP.scala 26:19 27:13]
  assign io_pmp_6_addr = reg_pmp_6_addr; // @[PMP.scala 26:19 28:14]
  assign io_pmp_6_mask = _T_312[31:0]; // @[PMP.scala 26:19 29:14]
  assign io_pmp_7_cfg_l = reg_pmp_7_cfg_l; // @[PMP.scala 26:19 27:13]
  assign io_pmp_7_cfg_res = 2'h0; // @[PMP.scala 26:19 27:13]
  assign io_pmp_7_cfg_a = reg_pmp_7_cfg_a; // @[PMP.scala 26:19 27:13]
  assign io_pmp_7_cfg_x = reg_pmp_7_cfg_x; // @[PMP.scala 26:19 27:13]
  assign io_pmp_7_cfg_w = reg_pmp_7_cfg_w; // @[PMP.scala 26:19 27:13]
  assign io_pmp_7_cfg_r = reg_pmp_7_cfg_r; // @[PMP.scala 26:19 27:13]
  assign io_pmp_7_addr = reg_pmp_7_addr; // @[PMP.scala 26:19 28:14]
  assign io_pmp_7_mask = _T_321[31:0]; // @[PMP.scala 26:19 29:14]
  assign io_counters_0_eventSel = reg_hpmevent_0; // @[CSR.scala 406:70]
  assign io_counters_1_eventSel = reg_hpmevent_1; // @[CSR.scala 406:70]
  assign io_csrw_counter = _T_3636[31:0]; // @[CSR.scala 798:19]
  assign io_trace_0_valid = io_retire > 1'h0 | io_trace_0_exception; // @[CSR.scala 1037:30]
  assign io_trace_0_iaddr = io_pc; // @[CSR.scala 1039:13]
  assign io_trace_0_insn = io_inst_0; // @[CSR.scala 1038:12]
  assign io_trace_0_priv = {reg_debug,reg_mstatus_prv}; // @[Cat.scala 29:58]
  assign io_trace_0_exception = insn_call | insn_break | io_exception; // @[CSR.scala 661:43]
  assign io_trace_0_interrupt = cause[63]; // @[CSR.scala 1042:25]
  assign io_trace_0_cause = cause[7:0]; // @[CSR.scala 1041:13]
  assign io_trace_0_tval = io_tval; // @[CSR.scala 1043:12]
  assign io_customCSRs_0_wen = csr_wen & _T_706; // @[CSR.scala 752:12 799:18]
  assign io_customCSRs_0_wdata = _T_712 & _T_716; // @[CSR.scala 1058:43]
  assign io_customCSRs_0_value = reg_custom_0; // @[CSR.scala 754:14]
  always @(posedge clock) begin
    if (reset) begin // @[CSR.scala 300:24]
      reg_mstatus_prv <= 2'h3; // @[CSR.scala 300:24]
    end else if (new_prv == 2'h2) begin // @[CSR.scala 1062:29]
      reg_mstatus_prv <= 2'h0;
    end else if (insn_ret) begin // @[CSR.scala 726:19]
      if (~io_rw_addr[9]) begin // @[CSR.scala 727:52]
        reg_mstatus_prv <= {{1'd0}, reg_mstatus_spp}; // @[CSR.scala 731:15]
      end else begin
        reg_mstatus_prv <= _GEN_95;
      end
    end else if (exception) begin // @[CSR.scala 676:20]
      reg_mstatus_prv <= _GEN_63;
    end
    if (reset) begin // @[CSR.scala 300:24]
      reg_mstatus_tsr <= 1'h0; // @[CSR.scala 300:24]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_569) begin // @[CSR.scala 800:39]
        reg_mstatus_tsr <= _T_3638[22]; // @[CSR.scala 813:27]
      end
    end
    if (reset) begin // @[CSR.scala 300:24]
      reg_mstatus_tw <= 1'h0; // @[CSR.scala 300:24]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_569) begin // @[CSR.scala 800:39]
        reg_mstatus_tw <= _T_3638[21]; // @[CSR.scala 812:26]
      end
    end
    if (reset) begin // @[CSR.scala 300:24]
      reg_mstatus_tvm <= 1'h0; // @[CSR.scala 300:24]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_569) begin // @[CSR.scala 800:39]
        reg_mstatus_tvm <= _T_3638[20]; // @[CSR.scala 818:27]
      end
    end
    if (reset) begin // @[CSR.scala 300:24]
      reg_mstatus_mxr <= 1'h0; // @[CSR.scala 300:24]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_676) begin // @[CSR.scala 886:41]
        reg_mstatus_mxr <= _T_3638[19]; // @[CSR.scala 894:27]
      end else if (_T_569) begin // @[CSR.scala 800:39]
        reg_mstatus_mxr <= _T_3638[19]; // @[CSR.scala 816:27]
      end
    end
    if (reset) begin // @[CSR.scala 300:24]
      reg_mstatus_sum <= 1'h0; // @[CSR.scala 300:24]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_676) begin // @[CSR.scala 886:41]
        reg_mstatus_sum <= _T_3638[18]; // @[CSR.scala 895:27]
      end else if (_T_569) begin // @[CSR.scala 800:39]
        reg_mstatus_sum <= _T_3638[18]; // @[CSR.scala 817:27]
      end
    end
    if (reset) begin // @[CSR.scala 300:24]
      reg_mstatus_mprv <= 1'h0; // @[CSR.scala 300:24]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_569) begin // @[CSR.scala 800:39]
        reg_mstatus_mprv <= _T_3638[17]; // @[CSR.scala 806:26]
      end
    end
    if (reset) begin // @[CSR.scala 300:24]
      reg_mstatus_fs <= 2'h0; // @[CSR.scala 300:24]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_676) begin // @[CSR.scala 886:41]
        reg_mstatus_fs <= _T_3638[14:13]; // @[CSR.scala 891:24]
      end else if (_T_569) begin // @[CSR.scala 800:39]
        reg_mstatus_fs <= _T_3638[14:13]; // @[CSR.scala 822:55]
      end else begin
        reg_mstatus_fs <= _GEN_120;
      end
    end else begin
      reg_mstatus_fs <= _GEN_120;
    end
    if (reset) begin // @[CSR.scala 300:24]
      reg_mstatus_mpp <= 2'h3; // @[CSR.scala 300:24]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_569) begin // @[CSR.scala 800:39]
        if (_T_3638[12:11] == 2'h2) begin // @[CSR.scala 1062:29]
          reg_mstatus_mpp <= 2'h0;
        end else begin
          reg_mstatus_mpp <= _T_3638[12:11];
        end
      end else begin
        reg_mstatus_mpp <= _GEN_118;
      end
    end else begin
      reg_mstatus_mpp <= _GEN_118;
    end
    reg_mstatus_spp <= _GEN_420[0]; // @[CSR.scala 300:{24,24}]
    if (reset) begin // @[CSR.scala 300:24]
      reg_mstatus_mpie <= 1'h0; // @[CSR.scala 300:24]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_569) begin // @[CSR.scala 800:39]
        reg_mstatus_mpie <= _T_3638[7]; // @[CSR.scala 803:24]
      end else begin
        reg_mstatus_mpie <= _GEN_117;
      end
    end else begin
      reg_mstatus_mpie <= _GEN_117;
    end
    if (reset) begin // @[CSR.scala 300:24]
      reg_mstatus_spie <= 1'h0; // @[CSR.scala 300:24]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_676) begin // @[CSR.scala 886:41]
        reg_mstatus_spie <= _T_3638[5]; // @[CSR.scala 889:26]
      end else if (_T_569) begin // @[CSR.scala 800:39]
        reg_mstatus_spie <= _T_3638[5]; // @[CSR.scala 810:28]
      end else begin
        reg_mstatus_spie <= _GEN_111;
      end
    end else begin
      reg_mstatus_spie <= _GEN_111;
    end
    if (reset) begin // @[CSR.scala 300:24]
      reg_mstatus_mie <= 1'h0; // @[CSR.scala 300:24]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_569) begin // @[CSR.scala 800:39]
        reg_mstatus_mie <= _T_3638[3]; // @[CSR.scala 802:23]
      end else begin
        reg_mstatus_mie <= _GEN_116;
      end
    end else begin
      reg_mstatus_mie <= _GEN_116;
    end
    if (reset) begin // @[CSR.scala 300:24]
      reg_mstatus_sie <= 1'h0; // @[CSR.scala 300:24]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_676) begin // @[CSR.scala 886:41]
        reg_mstatus_sie <= _T_3638[1]; // @[CSR.scala 888:25]
      end else if (_T_569) begin // @[CSR.scala 800:39]
        reg_mstatus_sie <= _T_3638[1]; // @[CSR.scala 811:27]
      end else begin
        reg_mstatus_sie <= _GEN_110;
      end
    end else begin
      reg_mstatus_sie <= _GEN_110;
    end
    if (reset) begin // @[CSR.scala 308:21]
      reg_dcsr_prv <= 2'h3; // @[CSR.scala 308:21]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_578) begin // @[CSR.scala 874:38]
        if (wdata[1:0] == 2'h2) begin // @[CSR.scala 1062:29]
          reg_dcsr_prv <= 2'h0;
        end else begin
          reg_dcsr_prv <= wdata[1:0];
        end
      end else begin
        reg_dcsr_prv <= _GEN_80;
      end
    end else begin
      reg_dcsr_prv <= _GEN_80;
    end
    if (_T_244) begin // @[CSR.scala 668:25]
      reg_singleStepped <= 1'h0; // @[CSR.scala 668:45]
    end else begin
      reg_singleStepped <= _GEN_38;
    end
    if (reset) begin // @[CSR.scala 308:21]
      reg_dcsr_ebreakm <= 1'h0; // @[CSR.scala 308:21]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_578) begin // @[CSR.scala 874:38]
        reg_dcsr_ebreakm <= wdata[15]; // @[CSR.scala 877:26]
      end
    end
    if (reset) begin // @[CSR.scala 308:21]
      reg_dcsr_ebreaks <= 1'h0; // @[CSR.scala 308:21]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_578) begin // @[CSR.scala 874:38]
        reg_dcsr_ebreaks <= wdata[13]; // @[CSR.scala 878:47]
      end
    end
    if (reset) begin // @[CSR.scala 308:21]
      reg_dcsr_ebreaku <= 1'h0; // @[CSR.scala 308:21]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_578) begin // @[CSR.scala 874:38]
        reg_dcsr_ebreaku <= wdata[12]; // @[CSR.scala 879:41]
      end
    end
    if (reset) begin // @[CSR.scala 349:22]
      reg_debug <= 1'h0; // @[CSR.scala 349:22]
    end else if (insn_ret) begin // @[CSR.scala 726:19]
      if (~io_rw_addr[9]) begin // @[CSR.scala 727:52]
        reg_debug <= _GEN_77;
      end else if (io_rw_addr[10]) begin // @[CSR.scala 733:53]
        reg_debug <= 1'h0; // @[CSR.scala 735:17]
      end else begin
        reg_debug <= _GEN_77;
      end
    end else begin
      reg_debug <= _GEN_77;
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_686) begin // @[CSR.scala 920:42]
        reg_mideleg <= wdata; // @[CSR.scala 920:56]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_687) begin // @[CSR.scala 921:42]
        reg_medeleg <= wdata; // @[CSR.scala 921:56]
      end
    end
    if (reset) begin // @[CSR.scala 308:21]
      reg_dcsr_cause <= 3'h0; // @[CSR.scala 308:21]
    end else if (exception) begin // @[CSR.scala 676:20]
      if (trapToDebug) begin // @[CSR.scala 677:24]
        if (~reg_debug) begin // @[CSR.scala 678:25]
          reg_dcsr_cause <= _T_1249; // @[CSR.scala 681:24]
        end
      end
    end
    if (reset) begin // @[CSR.scala 308:21]
      reg_dcsr_step <= 1'h0; // @[CSR.scala 308:21]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_578) begin // @[CSR.scala 874:38]
        reg_dcsr_step <= wdata[2]; // @[CSR.scala 876:23]
      end
    end
    reg_dpc <= _GEN_291[39:0];
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_580) begin // @[CSR.scala 883:42]
        reg_dscratch <= wdata; // @[CSR.scala 883:57]
      end
    end
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_0_cfg_l <= 1'h0; // @[PMP.scala 40:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_0_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_0_cfg_l <= wdata[7]; // @[CSR.scala 953:17]
      end
    end
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_0_cfg_a <= 2'h0; // @[PMP.scala 39:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_0_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_0_cfg_a <= wdata[4:3]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_0_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_0_cfg_x <= wdata[2]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_0_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_0_cfg_w <= wdata[1] & wdata[0]; // @[CSR.scala 955:19]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_0_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_0_cfg_r <= wdata[0]; // @[CSR.scala 953:17]
      end
    end
    reg_pmp_0_addr <= _GEN_310[29:0];
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_1_cfg_l <= 1'h0; // @[PMP.scala 40:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_1_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_1_cfg_l <= wdata[15]; // @[CSR.scala 953:17]
      end
    end
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_1_cfg_a <= 2'h0; // @[PMP.scala 39:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_1_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_1_cfg_a <= wdata[12:11]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_1_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_1_cfg_x <= wdata[10]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_1_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_1_cfg_w <= wdata[9] & wdata[8]; // @[CSR.scala 955:19]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_1_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_1_cfg_r <= wdata[8]; // @[CSR.scala 953:17]
      end
    end
    reg_pmp_1_addr <= _GEN_317[29:0];
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_2_cfg_l <= 1'h0; // @[PMP.scala 40:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_2_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_2_cfg_l <= wdata[23]; // @[CSR.scala 953:17]
      end
    end
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_2_cfg_a <= 2'h0; // @[PMP.scala 39:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_2_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_2_cfg_a <= wdata[20:19]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_2_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_2_cfg_x <= wdata[18]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_2_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_2_cfg_w <= wdata[17] & wdata[16]; // @[CSR.scala 955:19]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_2_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_2_cfg_r <= wdata[16]; // @[CSR.scala 953:17]
      end
    end
    reg_pmp_2_addr <= _GEN_324[29:0];
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_3_cfg_l <= 1'h0; // @[PMP.scala 40:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_3_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_3_cfg_l <= wdata[31]; // @[CSR.scala 953:17]
      end
    end
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_3_cfg_a <= 2'h0; // @[PMP.scala 39:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_3_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_3_cfg_a <= wdata[28:27]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_3_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_3_cfg_x <= wdata[26]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_3_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_3_cfg_w <= wdata[25] & wdata[24]; // @[CSR.scala 955:19]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_3_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_3_cfg_r <= wdata[24]; // @[CSR.scala 953:17]
      end
    end
    reg_pmp_3_addr <= _GEN_331[29:0];
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_4_cfg_l <= 1'h0; // @[PMP.scala 40:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_4_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_4_cfg_l <= wdata[39]; // @[CSR.scala 953:17]
      end
    end
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_4_cfg_a <= 2'h0; // @[PMP.scala 39:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_4_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_4_cfg_a <= wdata[36:35]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_4_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_4_cfg_x <= wdata[34]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_4_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_4_cfg_w <= wdata[33] & wdata[32]; // @[CSR.scala 955:19]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_4_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_4_cfg_r <= wdata[32]; // @[CSR.scala 953:17]
      end
    end
    reg_pmp_4_addr <= _GEN_338[29:0];
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_5_cfg_l <= 1'h0; // @[PMP.scala 40:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_5_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_5_cfg_l <= wdata[47]; // @[CSR.scala 953:17]
      end
    end
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_5_cfg_a <= 2'h0; // @[PMP.scala 39:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_5_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_5_cfg_a <= wdata[44:43]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_5_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_5_cfg_x <= wdata[42]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_5_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_5_cfg_w <= wdata[41] & wdata[40]; // @[CSR.scala 955:19]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_5_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_5_cfg_r <= wdata[40]; // @[CSR.scala 953:17]
      end
    end
    reg_pmp_5_addr <= _GEN_345[29:0];
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_6_cfg_l <= 1'h0; // @[PMP.scala 40:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_6_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_6_cfg_l <= wdata[55]; // @[CSR.scala 953:17]
      end
    end
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_6_cfg_a <= 2'h0; // @[PMP.scala 39:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_6_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_6_cfg_a <= wdata[52:51]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_6_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_6_cfg_x <= wdata[50]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_6_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_6_cfg_w <= wdata[49] & wdata[48]; // @[CSR.scala 955:19]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_6_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_6_cfg_r <= wdata[48]; // @[CSR.scala 953:17]
      end
    end
    reg_pmp_6_addr <= _GEN_352[29:0];
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_7_cfg_l <= 1'h0; // @[PMP.scala 40:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_7_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_7_cfg_l <= wdata[63]; // @[CSR.scala 953:17]
      end
    end
    if (reset) begin // @[CSR.scala 1032:18]
      reg_pmp_7_cfg_a <= 2'h0; // @[PMP.scala 39:11]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_7_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_7_cfg_a <= wdata[60:59]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_7_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_7_cfg_x <= wdata[58]; // @[CSR.scala 953:17]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_7_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_7_cfg_w <= wdata[57] & wdata[56]; // @[CSR.scala 955:19]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_688 & ~reg_pmp_7_cfg_l) begin // @[CSR.scala 951:76]
        reg_pmp_7_cfg_r <= wdata[56]; // @[CSR.scala 953:17]
      end
    end
    reg_pmp_7_addr <= _GEN_359[29:0];
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_678) begin // @[CSR.scala 914:42]
        reg_mie <= _T_3825; // @[CSR.scala 914:52]
      end else if (_T_572) begin // @[CSR.scala 847:40]
        reg_mie <= _T_3719; // @[CSR.scala 847:50]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_571) begin // @[CSR.scala 835:35]
        reg_mip_seip <= _T_3700[9]; // @[CSR.scala 844:22]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_571) begin // @[CSR.scala 835:35]
        reg_mip_stip <= _T_3700[5]; // @[CSR.scala 843:22]
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_677) begin // @[CSR.scala 899:37]
        reg_mip_ssip <= _T_3793[1]; // @[CSR.scala 901:22]
      end else if (_T_571) begin // @[CSR.scala 835:35]
        reg_mip_ssip <= _T_3700[1]; // @[CSR.scala 842:22]
      end
    end
    reg_mepc <= _GEN_268[39:0];
    if (reset) begin // @[CSR.scala 369:27]
      reg_mcause <= 64'h0; // @[CSR.scala 369:27]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_576) begin // @[CSR.scala 852:40]
        reg_mcause <= _T_3723; // @[CSR.scala 852:53]
      end else begin
        reg_mcause <= _GEN_90;
      end
    end else begin
      reg_mcause <= _GEN_90;
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_575) begin // @[CSR.scala 853:40]
        reg_mtval <= wdata[39:0]; // @[CSR.scala 853:52]
      end else begin
        reg_mtval <= _GEN_91;
      end
    end else begin
      reg_mtval <= _GEN_91;
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_573) begin // @[CSR.scala 849:40]
        reg_mscratch <= wdata; // @[CSR.scala 849:55]
      end
    end
    reg_mtvec <= _GEN_421[31:0]; // @[CSR.scala 374:{27,27}]
    reg_mcounteren <= _GEN_303[31:0];
    reg_scounteren <= _GEN_302[31:0];
    reg_sepc <= _GEN_296[39:0];
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_680) begin // @[CSR.scala 918:42]
        reg_scause <= _T_3829; // @[CSR.scala 918:55]
      end else begin
        reg_scause <= _GEN_83;
      end
    end else begin
      reg_scause <= _GEN_83;
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_681) begin // @[CSR.scala 919:42]
        reg_stval <= wdata[39:0]; // @[CSR.scala 919:54]
      end else begin
        reg_stval <= _GEN_85;
      end
    end else begin
      reg_stval <= _GEN_85;
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_679) begin // @[CSR.scala 915:42]
        reg_sscratch <= wdata; // @[CSR.scala 915:57]
      end
    end
    reg_stvec <= _GEN_297[38:0];
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_682) begin // @[CSR.scala 903:38]
        if (_T_3819) begin // @[CSR.scala 907:62]
          reg_satp_mode <= _T_3820; // @[CSR.scala 908:27]
        end
      end
    end
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_682) begin // @[CSR.scala 903:38]
        if (_T_3819) begin // @[CSR.scala 907:62]
          reg_satp_ppn <= {{24'd0}, wdata[19:0]}; // @[CSR.scala 909:26]
        end
      end
    end
    reg_fflags <= _GEN_284[4:0];
    reg_frm <= _GEN_285[2:0];
    _T_39 <= _GEN_422[5:0]; // @[Counters.scala 46:{37,37}]
    if (reset) begin // @[Counters.scala 51:27]
      _T_41 <= 58'h0; // @[Counters.scala 51:27]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_585) begin // @[CSR.scala 1076:31]
        _T_41 <= wdata[63:6]; // @[Counters.scala 67:23]
      end else begin
        _T_41 <= _GEN_0;
      end
    end else begin
      _T_41 <= _GEN_0;
    end
    if (reset) begin // @[CSR.scala 405:46]
      reg_hpmevent_0 <= 64'h0; // @[CSR.scala 405:46]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_586) begin // @[CSR.scala 857:45]
        reg_hpmevent_0 <= _T_3727; // @[CSR.scala 857:49]
      end
    end
    if (reset) begin // @[CSR.scala 405:46]
      reg_hpmevent_1 <= 64'h0; // @[CSR.scala 405:46]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_589) begin // @[CSR.scala 857:45]
        reg_hpmevent_1 <= _T_3727; // @[CSR.scala 857:49]
      end
    end
    _T_54 <= _GEN_273[5:0];
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_587) begin // @[CSR.scala 1076:31]
        _T_56 <= wdata[39:6]; // @[Counters.scala 67:23]
      end else begin
        _T_56 <= _GEN_2;
      end
    end else begin
      _T_56 <= _GEN_2;
    end
    _T_61 <= _GEN_276[5:0];
    if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_590) begin // @[CSR.scala 1076:31]
        _T_63 <= wdata[39:6]; // @[Counters.scala 67:23]
      end else begin
        _T_63 <= _GEN_3;
      end
    end else begin
      _T_63 <= _GEN_3;
    end
    if (reset) begin // @[CSR.scala 566:43]
      reg_custom_0 <= 64'h0; // @[CSR.scala 566:43]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_706) begin // @[CSR.scala 966:35]
        reg_custom_0 <= _T_3994; // @[CSR.scala 967:13]
      end
    end
    if (reg_mstatus_mprv & _T_1246) begin // @[CSR.scala 657:35]
      _T_1210 <= reg_mstatus_mpp;
    end else begin
      _T_1210 <= reg_mstatus_prv;
    end
    if (reset) begin // @[Reg.scala 27:20]
      _T_1585 <= 1'h0; // @[Reg.scala 27:20]
    end else begin
      _T_1585 <= _GEN_119;
    end
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (~(_T_1216 <= 3'h1 | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed: these conditions must be mutually exclusive\n    at CSR.scala:662 assert(PopCount(insn_ret :: insn_call :: insn_break :: io.exception :: Nil) <= 1, \"these conditions must be mutually exclusive\")\n"
            ); // @[CSR.scala 662:9]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (~(_T_1216 <= 3'h1 | reset)) begin
          $fatal; // @[CSR.scala 662:9]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (~(~reg_singleStepped | ~io_retire | reset)) begin
          $fwrite(32'h80000002,
            "Assertion failed\n    at CSR.scala:670 assert(!reg_singleStepped || io.retire === UInt(0))\n"); // @[CSR.scala 670:9]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (~(~reg_singleStepped | ~io_retire | reset)) begin
          $fatal; // @[CSR.scala 670:9]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (set_fs_dirty & ~(reg_mstatus_fs > 2'h0 | reset)) begin
          $fwrite(32'h80000002,"Assertion failed\n    at CSR.scala:779 assert(reg_mstatus.fs > 0)\n"); // @[CSR.scala 779:13]
        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif // SYNTHESIS
    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (set_fs_dirty & ~(reg_mstatus_fs > 2'h0 | reset)) begin
          $fatal; // @[CSR.scala 779:13]
        end
    `ifdef STOP_COND
      end
    `endif
    `endif // SYNTHESIS
  end
  always @(posedge io_ungated_clock) begin
    if (reset) begin // @[CSR.scala 394:50]
      reg_wfi <= 1'h0; // @[CSR.scala 394:50]
    end else if (|pending_interrupts | io_interrupts_debug | exception) begin // @[CSR.scala 665:69]
      reg_wfi <= 1'h0; // @[CSR.scala 665:79]
    end else begin
      reg_wfi <= _GEN_36;
    end
    _T_47 <= _GEN_423[5:0]; // @[Counters.scala 46:{37,37}]
    if (reset) begin // @[Counters.scala 51:27]
      _T_49 <= 58'h0; // @[Counters.scala 51:27]
    end else if (csr_wen) begin // @[CSR.scala 799:18]
      if (_T_584) begin // @[CSR.scala 1076:31]
        _T_49 <= wdata[63:6]; // @[Counters.scala 67:23]
      end else begin
        _T_49 <= _GEN_1;
      end
    end else begin
      _T_49 <= _GEN_1;
    end
  end
// Register and memory initialization
`ifdef RANDOMIZE_GARBAGE_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_INVALID_ASSIGN
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_REG_INIT
`define RANDOMIZE
`endif
`ifdef RANDOMIZE_MEM_INIT
`define RANDOMIZE
`endif
`ifndef RANDOM
`define RANDOM $random
`endif
`ifdef RANDOMIZE_MEM_INIT
  integer initvar;
`endif
`ifndef SYNTHESIS
`ifdef FIRRTL_BEFORE_INITIAL
`FIRRTL_BEFORE_INITIAL
`endif
initial begin
  `ifdef RANDOMIZE
    `ifdef INIT_RANDOM
      `INIT_RANDOM
    `endif
    `ifndef VERILATOR
      `ifdef RANDOMIZE_DELAY
        #`RANDOMIZE_DELAY begin end
      `else
        #0.002 begin end
      `endif
    `endif
`ifdef RANDOMIZE_REG_INIT
  _RAND_0 = {1{`RANDOM}};
  reg_mstatus_prv = _RAND_0[1:0];
  _RAND_1 = {1{`RANDOM}};
  reg_mstatus_tsr = _RAND_1[0:0];
  _RAND_2 = {1{`RANDOM}};
  reg_mstatus_tw = _RAND_2[0:0];
  _RAND_3 = {1{`RANDOM}};
  reg_mstatus_tvm = _RAND_3[0:0];
  _RAND_4 = {1{`RANDOM}};
  reg_mstatus_mxr = _RAND_4[0:0];
  _RAND_5 = {1{`RANDOM}};
  reg_mstatus_sum = _RAND_5[0:0];
  _RAND_6 = {1{`RANDOM}};
  reg_mstatus_mprv = _RAND_6[0:0];
  _RAND_7 = {1{`RANDOM}};
  reg_mstatus_fs = _RAND_7[1:0];
  _RAND_8 = {1{`RANDOM}};
  reg_mstatus_mpp = _RAND_8[1:0];
  _RAND_9 = {1{`RANDOM}};
  reg_mstatus_spp = _RAND_9[0:0];
  _RAND_10 = {1{`RANDOM}};
  reg_mstatus_mpie = _RAND_10[0:0];
  _RAND_11 = {1{`RANDOM}};
  reg_mstatus_spie = _RAND_11[0:0];
  _RAND_12 = {1{`RANDOM}};
  reg_mstatus_mie = _RAND_12[0:0];
  _RAND_13 = {1{`RANDOM}};
  reg_mstatus_sie = _RAND_13[0:0];
  _RAND_14 = {1{`RANDOM}};
  reg_dcsr_prv = _RAND_14[1:0];
  _RAND_15 = {1{`RANDOM}};
  reg_singleStepped = _RAND_15[0:0];
  _RAND_16 = {1{`RANDOM}};
  reg_dcsr_ebreakm = _RAND_16[0:0];
  _RAND_17 = {1{`RANDOM}};
  reg_dcsr_ebreaks = _RAND_17[0:0];
  _RAND_18 = {1{`RANDOM}};
  reg_dcsr_ebreaku = _RAND_18[0:0];
  _RAND_19 = {1{`RANDOM}};
  reg_debug = _RAND_19[0:0];
  _RAND_20 = {2{`RANDOM}};
  reg_mideleg = _RAND_20[63:0];
  _RAND_21 = {2{`RANDOM}};
  reg_medeleg = _RAND_21[63:0];
  _RAND_22 = {1{`RANDOM}};
  reg_dcsr_cause = _RAND_22[2:0];
  _RAND_23 = {1{`RANDOM}};
  reg_dcsr_step = _RAND_23[0:0];
  _RAND_24 = {2{`RANDOM}};
  reg_dpc = _RAND_24[39:0];
  _RAND_25 = {2{`RANDOM}};
  reg_dscratch = _RAND_25[63:0];
  _RAND_26 = {1{`RANDOM}};
  reg_pmp_0_cfg_l = _RAND_26[0:0];
  _RAND_27 = {1{`RANDOM}};
  reg_pmp_0_cfg_a = _RAND_27[1:0];
  _RAND_28 = {1{`RANDOM}};
  reg_pmp_0_cfg_x = _RAND_28[0:0];
  _RAND_29 = {1{`RANDOM}};
  reg_pmp_0_cfg_w = _RAND_29[0:0];
  _RAND_30 = {1{`RANDOM}};
  reg_pmp_0_cfg_r = _RAND_30[0:0];
  _RAND_31 = {1{`RANDOM}};
  reg_pmp_0_addr = _RAND_31[29:0];
  _RAND_32 = {1{`RANDOM}};
  reg_pmp_1_cfg_l = _RAND_32[0:0];
  _RAND_33 = {1{`RANDOM}};
  reg_pmp_1_cfg_a = _RAND_33[1:0];
  _RAND_34 = {1{`RANDOM}};
  reg_pmp_1_cfg_x = _RAND_34[0:0];
  _RAND_35 = {1{`RANDOM}};
  reg_pmp_1_cfg_w = _RAND_35[0:0];
  _RAND_36 = {1{`RANDOM}};
  reg_pmp_1_cfg_r = _RAND_36[0:0];
  _RAND_37 = {1{`RANDOM}};
  reg_pmp_1_addr = _RAND_37[29:0];
  _RAND_38 = {1{`RANDOM}};
  reg_pmp_2_cfg_l = _RAND_38[0:0];
  _RAND_39 = {1{`RANDOM}};
  reg_pmp_2_cfg_a = _RAND_39[1:0];
  _RAND_40 = {1{`RANDOM}};
  reg_pmp_2_cfg_x = _RAND_40[0:0];
  _RAND_41 = {1{`RANDOM}};
  reg_pmp_2_cfg_w = _RAND_41[0:0];
  _RAND_42 = {1{`RANDOM}};
  reg_pmp_2_cfg_r = _RAND_42[0:0];
  _RAND_43 = {1{`RANDOM}};
  reg_pmp_2_addr = _RAND_43[29:0];
  _RAND_44 = {1{`RANDOM}};
  reg_pmp_3_cfg_l = _RAND_44[0:0];
  _RAND_45 = {1{`RANDOM}};
  reg_pmp_3_cfg_a = _RAND_45[1:0];
  _RAND_46 = {1{`RANDOM}};
  reg_pmp_3_cfg_x = _RAND_46[0:0];
  _RAND_47 = {1{`RANDOM}};
  reg_pmp_3_cfg_w = _RAND_47[0:0];
  _RAND_48 = {1{`RANDOM}};
  reg_pmp_3_cfg_r = _RAND_48[0:0];
  _RAND_49 = {1{`RANDOM}};
  reg_pmp_3_addr = _RAND_49[29:0];
  _RAND_50 = {1{`RANDOM}};
  reg_pmp_4_cfg_l = _RAND_50[0:0];
  _RAND_51 = {1{`RANDOM}};
  reg_pmp_4_cfg_a = _RAND_51[1:0];
  _RAND_52 = {1{`RANDOM}};
  reg_pmp_4_cfg_x = _RAND_52[0:0];
  _RAND_53 = {1{`RANDOM}};
  reg_pmp_4_cfg_w = _RAND_53[0:0];
  _RAND_54 = {1{`RANDOM}};
  reg_pmp_4_cfg_r = _RAND_54[0:0];
  _RAND_55 = {1{`RANDOM}};
  reg_pmp_4_addr = _RAND_55[29:0];
  _RAND_56 = {1{`RANDOM}};
  reg_pmp_5_cfg_l = _RAND_56[0:0];
  _RAND_57 = {1{`RANDOM}};
  reg_pmp_5_cfg_a = _RAND_57[1:0];
  _RAND_58 = {1{`RANDOM}};
  reg_pmp_5_cfg_x = _RAND_58[0:0];
  _RAND_59 = {1{`RANDOM}};
  reg_pmp_5_cfg_w = _RAND_59[0:0];
  _RAND_60 = {1{`RANDOM}};
  reg_pmp_5_cfg_r = _RAND_60[0:0];
  _RAND_61 = {1{`RANDOM}};
  reg_pmp_5_addr = _RAND_61[29:0];
  _RAND_62 = {1{`RANDOM}};
  reg_pmp_6_cfg_l = _RAND_62[0:0];
  _RAND_63 = {1{`RANDOM}};
  reg_pmp_6_cfg_a = _RAND_63[1:0];
  _RAND_64 = {1{`RANDOM}};
  reg_pmp_6_cfg_x = _RAND_64[0:0];
  _RAND_65 = {1{`RANDOM}};
  reg_pmp_6_cfg_w = _RAND_65[0:0];
  _RAND_66 = {1{`RANDOM}};
  reg_pmp_6_cfg_r = _RAND_66[0:0];
  _RAND_67 = {1{`RANDOM}};
  reg_pmp_6_addr = _RAND_67[29:0];
  _RAND_68 = {1{`RANDOM}};
  reg_pmp_7_cfg_l = _RAND_68[0:0];
  _RAND_69 = {1{`RANDOM}};
  reg_pmp_7_cfg_a = _RAND_69[1:0];
  _RAND_70 = {1{`RANDOM}};
  reg_pmp_7_cfg_x = _RAND_70[0:0];
  _RAND_71 = {1{`RANDOM}};
  reg_pmp_7_cfg_w = _RAND_71[0:0];
  _RAND_72 = {1{`RANDOM}};
  reg_pmp_7_cfg_r = _RAND_72[0:0];
  _RAND_73 = {1{`RANDOM}};
  reg_pmp_7_addr = _RAND_73[29:0];
  _RAND_74 = {2{`RANDOM}};
  reg_mie = _RAND_74[63:0];
  _RAND_75 = {1{`RANDOM}};
  reg_mip_seip = _RAND_75[0:0];
  _RAND_76 = {1{`RANDOM}};
  reg_mip_stip = _RAND_76[0:0];
  _RAND_77 = {1{`RANDOM}};
  reg_mip_ssip = _RAND_77[0:0];
  _RAND_78 = {2{`RANDOM}};
  reg_mepc = _RAND_78[39:0];
  _RAND_79 = {2{`RANDOM}};
  reg_mcause = _RAND_79[63:0];
  _RAND_80 = {2{`RANDOM}};
  reg_mtval = _RAND_80[39:0];
  _RAND_81 = {2{`RANDOM}};
  reg_mscratch = _RAND_81[63:0];
  _RAND_82 = {1{`RANDOM}};
  reg_mtvec = _RAND_82[31:0];
  _RAND_83 = {1{`RANDOM}};
  reg_mcounteren = _RAND_83[31:0];
  _RAND_84 = {1{`RANDOM}};
  reg_scounteren = _RAND_84[31:0];
  _RAND_85 = {2{`RANDOM}};
  reg_sepc = _RAND_85[39:0];
  _RAND_86 = {2{`RANDOM}};
  reg_scause = _RAND_86[63:0];
  _RAND_87 = {2{`RANDOM}};
  reg_stval = _RAND_87[39:0];
  _RAND_88 = {2{`RANDOM}};
  reg_sscratch = _RAND_88[63:0];
  _RAND_89 = {2{`RANDOM}};
  reg_stvec = _RAND_89[38:0];
  _RAND_90 = {1{`RANDOM}};
  reg_satp_mode = _RAND_90[3:0];
  _RAND_91 = {2{`RANDOM}};
  reg_satp_ppn = _RAND_91[43:0];
  _RAND_92 = {1{`RANDOM}};
  reg_wfi = _RAND_92[0:0];
  _RAND_93 = {1{`RANDOM}};
  reg_fflags = _RAND_93[4:0];
  _RAND_94 = {1{`RANDOM}};
  reg_frm = _RAND_94[2:0];
  _RAND_95 = {1{`RANDOM}};
  _T_39 = _RAND_95[5:0];
  _RAND_96 = {2{`RANDOM}};
  _T_41 = _RAND_96[57:0];
  _RAND_97 = {1{`RANDOM}};
  _T_47 = _RAND_97[5:0];
  _RAND_98 = {2{`RANDOM}};
  _T_49 = _RAND_98[57:0];
  _RAND_99 = {2{`RANDOM}};
  reg_hpmevent_0 = _RAND_99[63:0];
  _RAND_100 = {2{`RANDOM}};
  reg_hpmevent_1 = _RAND_100[63:0];
  _RAND_101 = {1{`RANDOM}};
  _T_54 = _RAND_101[5:0];
  _RAND_102 = {2{`RANDOM}};
  _T_56 = _RAND_102[33:0];
  _RAND_103 = {1{`RANDOM}};
  _T_61 = _RAND_103[5:0];
  _RAND_104 = {2{`RANDOM}};
  _T_63 = _RAND_104[33:0];
  _RAND_105 = {2{`RANDOM}};
  reg_custom_0 = _RAND_105[63:0];
  _RAND_106 = {1{`RANDOM}};
  _T_1210 = _RAND_106[1:0];
  _RAND_107 = {1{`RANDOM}};
  _T_1585 = _RAND_107[0:0];
`endif // RANDOMIZE_REG_INIT
  `endif // RANDOMIZE
end // initial
`ifdef FIRRTL_AFTER_INITIAL
`FIRRTL_AFTER_INITIAL
`endif
`endif // SYNTHESIS
endmodule
