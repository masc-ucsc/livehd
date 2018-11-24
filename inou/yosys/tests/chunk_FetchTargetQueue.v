
module FetchTargetQueue(  input          clock,  input          reset,  output         io_enq_ready,  input          io_enq_valid,  input  [39:0]  io_enq_bits_fetch_pc,  input  [89:0]  io_enq_bits_history,  input  [1:0]   io_enq_bits_bim_info_value,  input  [9:0]   io_enq_bits_bim_info_entry_idx,  input          io_enq_bits_bim_info_br_seen,  input  [1:0]   io_enq_bits_bim_info_cfi_idx,  input  [141:0] io_enq_bits_bpd_info,  output [3:0]   io_enq_idx,  input          io_deq_valid,  input  [3:0]   io_deq_bits,  input  [3:0]   io_get_ftq_pc_ftq_idx,  output [39:0]  io_get_ftq_pc_fetch_pc,  output         io_get_ftq_pc_next_val,  output [39:0]  io_get_ftq_pc_next_pc,  output         io_restore_history_valid,  output [89:0]  io_restore_history_bits_history,  output         io_restore_history_bits_taken,  input          io_flush_valid,  input  [3:0]   io_flush_bits_ftq_idx,  input  [5:0]   io_flush_bits_pc_lob,  input  [2:0]   io_flush_bits_flush_typ,  output         io_take_pc_valid,  output [39:0]  io_take_pc_bits_addr,  input  [3:0]   io_com_ftq_idx,  output [39:0]  io_com_fetch_pc,  output         io_bim_update_valid,  output [9:0]   io_bim_update_bits_entry_idx,  output [1:0]   io_bim_update_bits_cfi_idx,  output [1:0]   io_bim_update_bits_cntr_value,  output         io_bim_update_bits_mispredicted,  output         io_bim_update_bits_taken,  output         io_bpd_update_valid,  output [38:0]  io_bpd_update_bits_fetch_pc,  output [89:0]  io_bpd_update_bits_history,  output         io_bpd_update_bits_mispredict,  output [1:0]   io_bpd_update_bits_miss_cfi_idx,  output         io_bpd_update_bits_taken,  output [141:0] io_bpd_update_bits_info,  input          io_brinfo_valid,  input          io_brinfo_mispredict,  input  [3:0]   io_brinfo_pc_lob,  input  [15:0]  io_brinfo_ftq_idx,  input          io_brinfo_taken,  input  [2:0]   io_brinfo_cfi_type);
  reg [39:0] ram_fetch_pc [0:15];  reg [63:0] _RAND_0;
  wire [39:0] ram_fetch_pc__T_1746_data;  wire [3:0] ram_fetch_pc__T_1746_addr;  wire [39:0] ram_fetch_pc__T_1818_data;  wire [3:0] ram_fetch_pc__T_1818_addr;  wire [39:0] ram_fetch_pc__T_1845_data;  wire [3:0] ram_fetch_pc__T_1845_addr;  wire [39:0] ram_fetch_pc__T_1875_data;  wire [3:0] ram_fetch_pc__T_1875_addr;  wire [39:0] ram_fetch_pc__T_1913_data;  wire [3:0] ram_fetch_pc__T_1913_addr;  wire [39:0] ram_fetch_pc__T_1386_data;  wire [3:0] ram_fetch_pc__T_1386_addr;  wire  ram_fetch_pc__T_1386_mask;  wire  ram_fetch_pc__T_1386_en;  reg [89:0] ram_history [0:15];  reg [95:0] _RAND_1;
  wire [89:0] ram_history__T_1746_data;  wire [3:0] ram_history__T_1746_addr;  wire [89:0] ram_history__T_1818_data;  wire [3:0] ram_history__T_1818_addr;  wire [89:0] ram_history__T_1845_data;  wire [3:0] ram_history__T_1845_addr;  wire [89:0] ram_history__T_1875_data;  wire [3:0] ram_history__T_1875_addr;  wire [89:0] ram_history__T_1913_data;  wire [3:0] ram_history__T_1913_addr;  wire [89:0] ram_history__T_1386_data;  wire [3:0] ram_history__T_1386_addr;  wire  ram_history__T_1386_mask;  wire  ram_history__T_1386_en;  reg [1:0] ram_bim_info_value [0:15];  reg [31:0] _RAND_2;
  wire [1:0] ram_bim_info_value__T_1746_data;  wire [3:0] ram_bim_info_value__T_1746_addr;  wire [1:0] ram_bim_info_value__T_1818_data;  wire [3:0] ram_bim_info_value__T_1818_addr;  wire [1:0] ram_bim_info_value__T_1845_data;  wire [3:0] ram_bim_info_value__T_1845_addr;  wire [1:0] ram_bim_info_value__T_1875_data;  wire [3:0] ram_bim_info_value__T_1875_addr;  wire [1:0] ram_bim_info_value__T_1913_data;  wire [3:0] ram_bim_info_value__T_1913_addr;  wire [1:0] ram_bim_info_value__T_1386_data;  wire [3:0] ram_bim_info_value__T_1386_addr;  wire  ram_bim_info_value__T_1386_mask;  wire  ram_bim_info_value__T_1386_en;  reg [9:0] ram_bim_info_entry_idx [0:15];  reg [31:0] _RAND_3;
  wire [9:0] ram_bim_info_entry_idx__T_1746_data;  wire [3:0] ram_bim_info_entry_idx__T_1746_addr;  wire [9:0] ram_bim_info_entry_idx__T_1818_data;  wire [3:0] ram_bim_info_entry_idx__T_1818_addr;  wire [9:0] ram_bim_info_entry_idx__T_1845_data;  wire [3:0] ram_bim_info_entry_idx__T_1845_addr;  wire [9:0] ram_bim_info_entry_idx__T_1875_data;  wire [3:0] ram_bim_info_entry_idx__T_1875_addr;  wire [9:0] ram_bim_info_entry_idx__T_1913_data;  wire [3:0] ram_bim_info_entry_idx__T_1913_addr;  wire [9:0] ram_bim_info_entry_idx__T_1386_data;  wire [3:0] ram_bim_info_entry_idx__T_1386_addr;  wire  ram_bim_info_entry_idx__T_1386_mask;  wire  ram_bim_info_entry_idx__T_1386_en;  reg [141:0] ram_bpd_info [0:15];  reg [159:0] _RAND_4;
  wire [141:0] ram_bpd_info__T_1746_data;  wire [3:0] ram_bpd_info__T_1746_addr;  wire [141:0] ram_bpd_info__T_1818_data;  wire [3:0] ram_bpd_info__T_1818_addr;  wire [141:0] ram_bpd_info__T_1845_data;  wire [3:0] ram_bpd_info__T_1845_addr;  wire [141:0] ram_bpd_info__T_1875_data;  wire [3:0] ram_bpd_info__T_1875_addr;  wire [141:0] ram_bpd_info__T_1913_data;  wire [3:0] ram_bpd_info__T_1913_addr;  wire [141:0] ram_bpd_info__T_1386_data;  wire [3:0] ram_bpd_info__T_1386_addr;  wire  ram_bpd_info__T_1386_mask;  wire  ram_bpd_info__T_1386_en;  reg [3:0] value;  reg [31:0] _RAND_5;
  reg [3:0] value_1;  reg [31:0] _RAND_6;
  reg  maybe_full;  reg [31:0] _RAND_7;
  wire  ptr_match;  wire  full;  reg [3:0] commit_ptr;  reg [31:0] _RAND_8;
  reg  cfi_info_0_executed;  reg [31:0] _RAND_9;
  reg  cfi_info_0_mispredicted;  reg [31:0] _RAND_10;
  reg  cfi_info_0_taken;  reg [31:0] _RAND_11;
  reg [1:0] cfi_info_0_cfi_idx;  reg [31:0] _RAND_12;
  reg [2:0] cfi_info_0_cfi_type;  reg [31:0] _RAND_13;
  reg  cfi_info_1_executed;  reg [31:0] _RAND_14;
  reg  cfi_info_1_mispredicted;  reg [31:0] _RAND_15;
  reg  cfi_info_1_taken;  reg [31:0] _RAND_16;
  reg [1:0] cfi_info_1_cfi_idx;  reg [31:0] _RAND_17;
  reg [2:0] cfi_info_1_cfi_type;  reg [31:0] _RAND_18;
  reg  cfi_info_2_executed;  reg [31:0] _RAND_19;
  reg  cfi_info_2_mispredicted;  reg [31:0] _RAND_20;
  reg  cfi_info_2_taken;  reg [31:0] _RAND_21;
  reg [1:0] cfi_info_2_cfi_idx;  reg [31:0] _RAND_22;
  reg [2:0] cfi_info_2_cfi_type;  reg [31:0] _RAND_23;
  reg  cfi_info_3_executed;  reg [31:0] _RAND_24;
  reg  cfi_info_3_mispredicted;  reg [31:0] _RAND_25;
  reg  cfi_info_3_taken;  reg [31:0] _RAND_26;
  reg [1:0] cfi_info_3_cfi_idx;  reg [31:0] _RAND_27;
  reg [2:0] cfi_info_3_cfi_type;  reg [31:0] _RAND_28;
  reg  cfi_info_4_executed;  reg [31:0] _RAND_29;
  reg  cfi_info_4_mispredicted;  reg [31:0] _RAND_30;
  reg  cfi_info_4_taken;  reg [31:0] _RAND_31;
  reg [1:0] cfi_info_4_cfi_idx;  reg [31:0] _RAND_32;
  reg [2:0] cfi_info_4_cfi_type;  reg [31:0] _RAND_33;
  reg  cfi_info_5_executed;  reg [31:0] _RAND_34;
  reg  cfi_info_5_mispredicted;  reg [31:0] _RAND_35;
  reg  cfi_info_5_taken;  reg [31:0] _RAND_36;
  reg [1:0] cfi_info_5_cfi_idx;  reg [31:0] _RAND_37;
  reg [2:0] cfi_info_5_cfi_type;  reg [31:0] _RAND_38;
  reg  cfi_info_6_executed;  reg [31:0] _RAND_39;
  reg  cfi_info_6_mispredicted;  reg [31:0] _RAND_40;
  reg  cfi_info_6_taken;  reg [31:0] _RAND_41;
  reg [1:0] cfi_info_6_cfi_idx;  reg [31:0] _RAND_42;
  reg [2:0] cfi_info_6_cfi_type;  reg [31:0] _RAND_43;
  reg  cfi_info_7_executed;  reg [31:0] _RAND_44;
  reg  cfi_info_7_mispredicted;  reg [31:0] _RAND_45;
  reg  cfi_info_7_taken;  reg [31:0] _RAND_46;
  reg [1:0] cfi_info_7_cfi_idx;  reg [31:0] _RAND_47;
  reg [2:0] cfi_info_7_cfi_type;  reg [31:0] _RAND_48;
  reg  cfi_info_8_executed;  reg [31:0] _RAND_49;
  reg  cfi_info_8_mispredicted;  reg [31:0] _RAND_50;
  reg  cfi_info_8_taken;  reg [31:0] _RAND_51;
  reg [1:0] cfi_info_8_cfi_idx;  reg [31:0] _RAND_52;
  reg [2:0] cfi_info_8_cfi_type;  reg [31:0] _RAND_53;
  reg  cfi_info_9_executed;  reg [31:0] _RAND_54;
  reg  cfi_info_9_mispredicted;  reg [31:0] _RAND_55;
  reg  cfi_info_9_taken;  reg [31:0] _RAND_56;
  reg [1:0] cfi_info_9_cfi_idx;  reg [31:0] _RAND_57;
  reg [2:0] cfi_info_9_cfi_type;  reg [31:0] _RAND_58;
  reg  cfi_info_10_executed;  reg [31:0] _RAND_59;
  reg  cfi_info_10_mispredicted;  reg [31:0] _RAND_60;
  reg  cfi_info_10_taken;  reg [31:0] _RAND_61;
  reg [1:0] cfi_info_10_cfi_idx;  reg [31:0] _RAND_62;
  reg [2:0] cfi_info_10_cfi_type;  reg [31:0] _RAND_63;
  reg  cfi_info_11_executed;  reg [31:0] _RAND_64;
  reg  cfi_info_11_mispredicted;  reg [31:0] _RAND_65;
  reg  cfi_info_11_taken;  reg [31:0] _RAND_66;
  reg [1:0] cfi_info_11_cfi_idx;  reg [31:0] _RAND_67;
  reg [2:0] cfi_info_11_cfi_type;  reg [31:0] _RAND_68;
  reg  cfi_info_12_executed;  reg [31:0] _RAND_69;
  reg  cfi_info_12_mispredicted;  reg [31:0] _RAND_70;
  reg  cfi_info_12_taken;  reg [31:0] _RAND_71;
  reg [1:0] cfi_info_12_cfi_idx;  reg [31:0] _RAND_72;
  reg [2:0] cfi_info_12_cfi_type;  reg [31:0] _RAND_73;
  reg  cfi_info_13_executed;  reg [31:0] _RAND_74;
  reg  cfi_info_13_mispredicted;  reg [31:0] _RAND_75;
  reg  cfi_info_13_taken;  reg [31:0] _RAND_76;
  reg [1:0] cfi_info_13_cfi_idx;  reg [31:0] _RAND_77;
  reg [2:0] cfi_info_13_cfi_type;  reg [31:0] _RAND_78;
  reg  cfi_info_14_executed;  reg [31:0] _RAND_79;
  reg  cfi_info_14_mispredicted;  reg [31:0] _RAND_80;
  reg  cfi_info_14_taken;  reg [31:0] _RAND_81;
  reg [1:0] cfi_info_14_cfi_idx;  reg [31:0] _RAND_82;
  reg [2:0] cfi_info_14_cfi_type;  reg [31:0] _RAND_83;
  reg  cfi_info_15_executed;  reg [31:0] _RAND_84;
  reg  cfi_info_15_mispredicted;  reg [31:0] _RAND_85;
  reg  cfi_info_15_taken;  reg [31:0] _RAND_86;
  reg [1:0] cfi_info_15_cfi_idx;  reg [31:0] _RAND_87;
  reg [2:0] cfi_info_15_cfi_type;  reg [31:0] _RAND_88;
  wire  _T_1380;  wire  _T_1383;  wire [2:0] _T_1454_cfi_type;  wire [3:0] _T_1471;  wire [3:0] _T_1474;  wire  _T_1475;  wire [3:0] _T_1478;  wire  _T_1480;  wire [15:0] _T_1482;  wire [3:0] _T_1483;  wire  _T_1484;  wire [3:0] _T_1512;  wire [1:0] _T_1569;  wire  _GEN_184;  wire [1:0] _GEN_186;  wire  _GEN_189;  wire [1:0] _GEN_191;  wire  _GEN_194;  wire [1:0] _GEN_196;  wire  _GEN_199;  wire [1:0] _GEN_201;  wire  _GEN_204;  wire [1:0] _GEN_206;  wire  _GEN_209;  wire [1:0] _GEN_211;  wire  _GEN_214;  wire [1:0] _GEN_216;  wire  _GEN_219;  wire [1:0] _GEN_221;  wire  _GEN_224;  wire [1:0] _GEN_226;  wire  _GEN_229;  wire [1:0] _GEN_231;  wire  _GEN_234;  wire [1:0] _GEN_236;  wire  _GEN_239;  wire [1:0] _GEN_241;  wire  _GEN_244;  wire [1:0] _GEN_246;  wire  _GEN_249;  wire [1:0] _GEN_251;  wire  _GEN_254;  wire [1:0] _GEN_256;  wire  _T_1570;  wire  _T_1571;  wire  _T_1572;  wire  _T_1573;  wire  _T_1574;  wire  _T_1688;  wire  _T_1689;  wire  _T_1800;  wire  _GEN_631;  wire  _GEN_632;  wire  _GEN_633;  wire [1:0] _GEN_634;  wire [2:0] _GEN_635;  wire  _GEN_636;  wire  _GEN_637;  wire  _GEN_638;  wire [1:0] _GEN_639;  wire [2:0] _GEN_640;  wire  _GEN_641;  wire  _GEN_642;  wire  _GEN_643;  wire [1:0] _GEN_644;  wire [2:0] _GEN_645;  wire  _GEN_646;  wire  _GEN_647;  wire  _GEN_648;  wire [1:0] _GEN_649;  wire [2:0] _GEN_650;  wire  _GEN_651;  wire  _GEN_652;  wire  _GEN_653;  wire [1:0] _GEN_654;  wire [2:0] _GEN_655;  wire  _GEN_656;  wire  _GEN_657;  wire  _GEN_658;  wire [1:0] _GEN_659;  wire [2:0] _GEN_660;  wire  _GEN_661;  wire  _GEN_662;  wire  _GEN_663;  wire [1:0] _GEN_664;  wire [2:0] _GEN_665;  wire  _GEN_666;  wire  _GEN_667;  wire  _GEN_668;  wire [1:0] _GEN_669;  wire [2:0] _GEN_670;  wire  _GEN_671;  wire  _GEN_672;  wire  _GEN_673;  wire [1:0] _GEN_674;  wire [2:0] _GEN_675;  wire  _GEN_676;  wire  _GEN_677;  wire  _GEN_678;  wire [1:0] _GEN_679;  wire [2:0] _GEN_680;  wire  _GEN_681;  wire  _GEN_682;  wire  _GEN_683;  wire [1:0] _GEN_684;  wire [2:0] _GEN_685;  wire  _GEN_686;  wire  _GEN_687;  wire  _GEN_688;  wire [1:0] _GEN_689;  wire [2:0] _GEN_690;  wire  _GEN_691;  wire  _GEN_692;  wire  _GEN_693;  wire [1:0] _GEN_694;  wire [2:0] _GEN_695;  wire  _GEN_696;  wire  _GEN_697;  wire  _GEN_698;  wire [1:0] _GEN_699;  wire [2:0] _GEN_700;  wire  _GEN_701;  wire  _GEN_702;  wire  _GEN_703;  wire [2:0] _GEN_705;  wire  _T_1801;  wire  _T_1802;  wire  _T_1803;  wire  _T_1804;  wire  _T_1805;  wire  _T_1806;  wire  _T_1807;  wire  _T_1808;  wire  _T_1809;  wire  _T_1810;  wire  _T_1811;  wire  _T_1812;  wire [39:0] _GEN_718;  wire [15:0] _T_1816;  wire [3:0] _T_1873;  wire  _T_1908;  wire  _T_1909;  wire  _T_1911;  reg [39:0] _T_1944;  reg [63:0] _RAND_89;
  wire [39:0] _GEN_823;  wire [39:0] com_pc;  wire [39:0] com_pc_plus4;  wire [39:0] _T_1940;  wire [39:0] _T_1941;  reg [3:0] _T_1950;  reg [31:0] _RAND_90;
  wire  _T_1951;  wire  _T_1953;  wire  _T_1954;  assign ram_fetch_pc__T_1746_addr = value;
  assign ram_fetch_pc__T_1746_data = ram_fetch_pc[ram_fetch_pc__T_1746_addr];  assign ram_fetch_pc__T_1818_addr = _T_1816[3:0];
  assign ram_fetch_pc__T_1818_data = ram_fetch_pc[ram_fetch_pc__T_1818_addr];  assign ram_fetch_pc__T_1845_addr = io_get_ftq_pc_ftq_idx;
  assign ram_fetch_pc__T_1845_data = ram_fetch_pc[ram_fetch_pc__T_1845_addr];  assign ram_fetch_pc__T_1875_addr = io_get_ftq_pc_ftq_idx + 4'h1;
  assign ram_fetch_pc__T_1875_data = ram_fetch_pc[ram_fetch_pc__T_1875_addr];  assign ram_fetch_pc__T_1913_addr = io_com_ftq_idx;
  assign ram_fetch_pc__T_1913_data = ram_fetch_pc[ram_fetch_pc__T_1913_addr];  assign ram_fetch_pc__T_1386_data = io_enq_bits_fetch_pc;
  assign ram_fetch_pc__T_1386_addr = value_1;
  assign ram_fetch_pc__T_1386_mask = 1'h1;
  assign ram_fetch_pc__T_1386_en = io_enq_ready & io_enq_valid;
  assign ram_history__T_1746_addr = value;
  assign ram_history__T_1746_data = ram_history[ram_history__T_1746_addr];  assign ram_history__T_1818_addr = _T_1816[3:0];
  assign ram_history__T_1818_data = ram_history[ram_history__T_1818_addr];  assign ram_history__T_1845_addr = io_get_ftq_pc_ftq_idx;
  assign ram_history__T_1845_data = ram_history[ram_history__T_1845_addr];  assign ram_history__T_1875_addr = io_get_ftq_pc_ftq_idx + 4'h1;
  assign ram_history__T_1875_data = ram_history[ram_history__T_1875_addr];  assign ram_history__T_1913_addr = io_com_ftq_idx;
  assign ram_history__T_1913_data = ram_history[ram_history__T_1913_addr];  assign ram_history__T_1386_data = io_enq_bits_history;
  assign ram_history__T_1386_addr = value_1;
  assign ram_history__T_1386_mask = 1'h1;
  assign ram_history__T_1386_en = io_enq_ready & io_enq_valid;
  assign ram_bim_info_value__T_1746_addr = value;
  assign ram_bim_info_value__T_1746_data = ram_bim_info_value[ram_bim_info_value__T_1746_addr];  assign ram_bim_info_value__T_1818_addr = _T_1816[3:0];
  assign ram_bim_info_value__T_1818_data = ram_bim_info_value[ram_bim_info_value__T_1818_addr];  assign ram_bim_info_value__T_1845_addr = io_get_ftq_pc_ftq_idx;
  assign ram_bim_info_value__T_1845_data = ram_bim_info_value[ram_bim_info_value__T_1845_addr];  assign ram_bim_info_value__T_1875_addr = io_get_ftq_pc_ftq_idx + 4'h1;
  assign ram_bim_info_value__T_1875_data = ram_bim_info_value[ram_bim_info_value__T_1875_addr];  assign ram_bim_info_value__T_1913_addr = io_com_ftq_idx;
  assign ram_bim_info_value__T_1913_data = ram_bim_info_value[ram_bim_info_value__T_1913_addr];  assign ram_bim_info_value__T_1386_data = io_enq_bits_bim_info_value;
  assign ram_bim_info_value__T_1386_addr = value_1;
  assign ram_bim_info_value__T_1386_mask = 1'h1;
  assign ram_bim_info_value__T_1386_en = io_enq_ready & io_enq_valid;
  assign ram_bim_info_entry_idx__T_1746_addr = value;
  assign ram_bim_info_entry_idx__T_1746_data = ram_bim_info_entry_idx[ram_bim_info_entry_idx__T_1746_addr];  assign ram_bim_info_entry_idx__T_1818_addr = _T_1816[3:0];
  assign ram_bim_info_entry_idx__T_1818_data = ram_bim_info_entry_idx[ram_bim_info_entry_idx__T_1818_addr];  assign ram_bim_info_entry_idx__T_1845_addr = io_get_ftq_pc_ftq_idx;
  assign ram_bim_info_entry_idx__T_1845_data = ram_bim_info_entry_idx[ram_bim_info_entry_idx__T_1845_addr];  assign ram_bim_info_entry_idx__T_1875_addr = io_get_ftq_pc_ftq_idx + 4'h1;
  assign ram_bim_info_entry_idx__T_1875_data = ram_bim_info_entry_idx[ram_bim_info_entry_idx__T_1875_addr];  assign ram_bim_info_entry_idx__T_1913_addr = io_com_ftq_idx;
  assign ram_bim_info_entry_idx__T_1913_data = ram_bim_info_entry_idx[ram_bim_info_entry_idx__T_1913_addr];  assign ram_bim_info_entry_idx__T_1386_data = io_enq_bits_bim_info_entry_idx;
  assign ram_bim_info_entry_idx__T_1386_addr = value_1;
  assign ram_bim_info_entry_idx__T_1386_mask = 1'h1;
  assign ram_bim_info_entry_idx__T_1386_en = io_enq_ready & io_enq_valid;
  assign ram_bpd_info__T_1746_addr = value;
  assign ram_bpd_info__T_1746_data = ram_bpd_info[ram_bpd_info__T_1746_addr];  assign ram_bpd_info__T_1818_addr = _T_1816[3:0];
  assign ram_bpd_info__T_1818_data = ram_bpd_info[ram_bpd_info__T_1818_addr];  assign ram_bpd_info__T_1845_addr = io_get_ftq_pc_ftq_idx;
  assign ram_bpd_info__T_1845_data = ram_bpd_info[ram_bpd_info__T_1845_addr];  assign ram_bpd_info__T_1875_addr = io_get_ftq_pc_ftq_idx + 4'h1;
  assign ram_bpd_info__T_1875_data = ram_bpd_info[ram_bpd_info__T_1875_addr];  assign ram_bpd_info__T_1913_addr = io_com_ftq_idx;
  assign ram_bpd_info__T_1913_data = ram_bpd_info[ram_bpd_info__T_1913_addr];  assign ram_bpd_info__T_1386_data = io_enq_bits_bpd_info;
  assign ram_bpd_info__T_1386_addr = value_1;
  assign ram_bpd_info__T_1386_mask = 1'h1;
  assign ram_bpd_info__T_1386_en = io_enq_ready & io_enq_valid;
  assign ptr_match = value_1 == value;  assign full = ptr_match & maybe_full;  assign _T_1380 = io_enq_ready & io_enq_valid;  assign _T_1383 = value != commit_ptr;  assign _T_1454_cfi_type = {{2'd0}, io_enq_bits_bim_info_br_seen};  assign _T_1471 = value_1 + 4'h1;  assign _T_1474 = value + 4'h1;  assign _T_1475 = _T_1380 != _T_1383;  assign _T_1478 = io_flush_bits_ftq_idx + 4'h1;  assign _T_1480 = io_brinfo_valid & io_brinfo_mispredict;  assign _T_1482 = io_brinfo_ftq_idx + 16'h1;  assign _T_1483 = _T_1482[3:0];  assign _T_1484 = value_1 == _T_1483;  assign _T_1512 = io_brinfo_ftq_idx[3:0];  assign _T_1569 = io_brinfo_pc_lob[3:2];  assign _GEN_184 = 4'h1 == _T_1512 ? cfi_info_1_mispredicted : cfi_info_0_mispredicted;  assign _GEN_186 = 4'h1 == _T_1512 ? cfi_info_1_cfi_idx : cfi_info_0_cfi_idx;  assign _GEN_189 = 4'h2 == _T_1512 ? cfi_info_2_mispredicted : _GEN_184;  assign _GEN_191 = 4'h2 == _T_1512 ? cfi_info_2_cfi_idx : _GEN_186;  assign _GEN_194 = 4'h3 == _T_1512 ? cfi_info_3_mispredicted : _GEN_189;  assign _GEN_196 = 4'h3 == _T_1512 ? cfi_info_3_cfi_idx : _GEN_191;  assign _GEN_199 = 4'h4 == _T_1512 ? cfi_info_4_mispredicted : _GEN_194;  assign _GEN_201 = 4'h4 == _T_1512 ? cfi_info_4_cfi_idx : _GEN_196;  assign _GEN_204 = 4'h5 == _T_1512 ? cfi_info_5_mispredicted : _GEN_199;  assign _GEN_206 = 4'h5 == _T_1512 ? cfi_info_5_cfi_idx : _GEN_201;  assign _GEN_209 = 4'h6 == _T_1512 ? cfi_info_6_mispredicted : _GEN_204;  assign _GEN_211 = 4'h6 == _T_1512 ? cfi_info_6_cfi_idx : _GEN_206;  assign _GEN_214 = 4'h7 == _T_1512 ? cfi_info_7_mispredicted : _GEN_209;  assign _GEN_216 = 4'h7 == _T_1512 ? cfi_info_7_cfi_idx : _GEN_211;  assign _GEN_219 = 4'h8 == _T_1512 ? cfi_info_8_mispredicted : _GEN_214;  assign _GEN_221 = 4'h8 == _T_1512 ? cfi_info_8_cfi_idx : _GEN_216;  assign _GEN_224 = 4'h9 == _T_1512 ? cfi_info_9_mispredicted : _GEN_219;  assign _GEN_226 = 4'h9 == _T_1512 ? cfi_info_9_cfi_idx : _GEN_221;  assign _GEN_229 = 4'ha == _T_1512 ? cfi_info_10_mispredicted : _GEN_224;  assign _GEN_231 = 4'ha == _T_1512 ? cfi_info_10_cfi_idx : _GEN_226;  assign _GEN_234 = 4'hb == _T_1512 ? cfi_info_11_mispredicted : _GEN_229;  assign _GEN_236 = 4'hb == _T_1512 ? cfi_info_11_cfi_idx : _GEN_231;  assign _GEN_239 = 4'hc == _T_1512 ? cfi_info_12_mispredicted : _GEN_234;  assign _GEN_241 = 4'hc == _T_1512 ? cfi_info_12_cfi_idx : _GEN_236;  assign _GEN_244 = 4'hd == _T_1512 ? cfi_info_13_mispredicted : _GEN_239;  assign _GEN_246 = 4'hd == _T_1512 ? cfi_info_13_cfi_idx : _GEN_241;  assign _GEN_249 = 4'he == _T_1512 ? cfi_info_14_mispredicted : _GEN_244;  assign _GEN_251 = 4'he == _T_1512 ? cfi_info_14_cfi_idx : _GEN_246;  assign _GEN_254 = 4'hf == _T_1512 ? cfi_info_15_mispredicted : _GEN_249;  assign _GEN_256 = 4'hf == _T_1512 ? cfi_info_15_cfi_idx : _GEN_251;  assign _T_1570 = _GEN_254 == 1'h0;  assign _T_1571 = io_brinfo_mispredict & _T_1570;  assign _T_1572 = _T_1569 < _GEN_256;  assign _T_1573 = io_brinfo_mispredict & _T_1572;  assign _T_1574 = _T_1571 | _T_1573;  assign _T_1688 = _T_1569 == _GEN_256;  assign _T_1689 = _T_1570 & _T_1688;  assign _T_1800 = ram_bim_info_value__T_1746_data == 2'h0;  assign _GEN_631 = 4'h1 == value ? cfi_info_1_executed : cfi_info_0_executed;  assign _GEN_632 = 4'h1 == value ? cfi_info_1_mispredicted : cfi_info_0_mispredicted;  assign _GEN_633 = 4'h1 == value ? cfi_info_1_taken : cfi_info_0_taken;  assign _GEN_634 = 4'h1 == value ? cfi_info_1_cfi_idx : cfi_info_0_cfi_idx;  assign _GEN_635 = 4'h1 == value ? cfi_info_1_cfi_type : cfi_info_0_cfi_type;  assign _GEN_636 = 4'h2 == value ? cfi_info_2_executed : _GEN_631;  assign _GEN_637 = 4'h2 == value ? cfi_info_2_mispredicted : _GEN_632;  assign _GEN_638 = 4'h2 == value ? cfi_info_2_taken : _GEN_633;  assign _GEN_639 = 4'h2 == value ? cfi_info_2_cfi_idx : _GEN_634;  assign _GEN_640 = 4'h2 == value ? cfi_info_2_cfi_type : _GEN_635;  assign _GEN_641 = 4'h3 == value ? cfi_info_3_executed : _GEN_636;  assign _GEN_642 = 4'h3 == value ? cfi_info_3_mispredicted : _GEN_637;  assign _GEN_643 = 4'h3 == value ? cfi_info_3_taken : _GEN_638;  assign _GEN_644 = 4'h3 == value ? cfi_info_3_cfi_idx : _GEN_639;  assign _GEN_645 = 4'h3 == value ? cfi_info_3_cfi_type : _GEN_640;  assign _GEN_646 = 4'h4 == value ? cfi_info_4_executed : _GEN_641;  assign _GEN_647 = 4'h4 == value ? cfi_info_4_mispredicted : _GEN_642;  assign _GEN_648 = 4'h4 == value ? cfi_info_4_taken : _GEN_643;  assign _GEN_649 = 4'h4 == value ? cfi_info_4_cfi_idx : _GEN_644;  assign _GEN_650 = 4'h4 == value ? cfi_info_4_cfi_type : _GEN_645;  assign _GEN_651 = 4'h5 == value ? cfi_info_5_executed : _GEN_646;  assign _GEN_652 = 4'h5 == value ? cfi_info_5_mispredicted : _GEN_647;  assign _GEN_653 = 4'h5 == value ? cfi_info_5_taken : _GEN_648;  assign _GEN_654 = 4'h5 == value ? cfi_info_5_cfi_idx : _GEN_649;  assign _GEN_655 = 4'h5 == value ? cfi_info_5_cfi_type : _GEN_650;  assign _GEN_656 = 4'h6 == value ? cfi_info_6_executed : _GEN_651;  assign _GEN_657 = 4'h6 == value ? cfi_info_6_mispredicted : _GEN_652;  assign _GEN_658 = 4'h6 == value ? cfi_info_6_taken : _GEN_653;  assign _GEN_659 = 4'h6 == value ? cfi_info_6_cfi_idx : _GEN_654;  assign _GEN_660 = 4'h6 == value ? cfi_info_6_cfi_type : _GEN_655;  assign _GEN_661 = 4'h7 == value ? cfi_info_7_executed : _GEN_656;  assign _GEN_662 = 4'h7 == value ? cfi_info_7_mispredicted : _GEN_657;  assign _GEN_663 = 4'h7 == value ? cfi_info_7_taken : _GEN_658;  assign _GEN_664 = 4'h7 == value ? cfi_info_7_cfi_idx : _GEN_659;  assign _GEN_665 = 4'h7 == value ? cfi_info_7_cfi_type : _GEN_660;  assign _GEN_666 = 4'h8 == value ? cfi_info_8_executed : _GEN_661;  assign _GEN_667 = 4'h8 == value ? cfi_info_8_mispredicted : _GEN_662;  assign _GEN_668 = 4'h8 == value ? cfi_info_8_taken : _GEN_663;  assign _GEN_669 = 4'h8 == value ? cfi_info_8_cfi_idx : _GEN_664;  assign _GEN_670 = 4'h8 == value ? cfi_info_8_cfi_type : _GEN_665;  assign _GEN_671 = 4'h9 == value ? cfi_info_9_executed : _GEN_666;  assign _GEN_672 = 4'h9 == value ? cfi_info_9_mispredicted : _GEN_667;  assign _GEN_673 = 4'h9 == value ? cfi_info_9_taken : _GEN_668;  assign _GEN_674 = 4'h9 == value ? cfi_info_9_cfi_idx : _GEN_669;  assign _GEN_675 = 4'h9 == value ? cfi_info_9_cfi_type : _GEN_670;  assign _GEN_676 = 4'ha == value ? cfi_info_10_executed : _GEN_671;  assign _GEN_677 = 4'ha == value ? cfi_info_10_mispredicted : _GEN_672;  assign _GEN_678 = 4'ha == value ? cfi_info_10_taken : _GEN_673;  assign _GEN_679 = 4'ha == value ? cfi_info_10_cfi_idx : _GEN_674;  assign _GEN_680 = 4'ha == value ? cfi_info_10_cfi_type : _GEN_675;  assign _GEN_681 = 4'hb == value ? cfi_info_11_executed : _GEN_676;  assign _GEN_682 = 4'hb == value ? cfi_info_11_mispredicted : _GEN_677;  assign _GEN_683 = 4'hb == value ? cfi_info_11_taken : _GEN_678;  assign _GEN_684 = 4'hb == value ? cfi_info_11_cfi_idx : _GEN_679;  assign _GEN_685 = 4'hb == value ? cfi_info_11_cfi_type : _GEN_680;  assign _GEN_686 = 4'hc == value ? cfi_info_12_executed : _GEN_681;  assign _GEN_687 = 4'hc == value ? cfi_info_12_mispredicted : _GEN_682;  assign _GEN_688 = 4'hc == value ? cfi_info_12_taken : _GEN_683;  assign _GEN_689 = 4'hc == value ? cfi_info_12_cfi_idx : _GEN_684;  assign _GEN_690 = 4'hc == value ? cfi_info_12_cfi_type : _GEN_685;  assign _GEN_691 = 4'hd == value ? cfi_info_13_executed : _GEN_686;  assign _GEN_692 = 4'hd == value ? cfi_info_13_mispredicted : _GEN_687;  assign _GEN_693 = 4'hd == value ? cfi_info_13_taken : _GEN_688;  assign _GEN_694 = 4'hd == value ? cfi_info_13_cfi_idx : _GEN_689;  assign _GEN_695 = 4'hd == value ? cfi_info_13_cfi_type : _GEN_690;  assign _GEN_696 = 4'he == value ? cfi_info_14_executed : _GEN_691;  assign _GEN_697 = 4'he == value ? cfi_info_14_mispredicted : _GEN_692;  assign _GEN_698 = 4'he == value ? cfi_info_14_taken : _GEN_693;  assign _GEN_699 = 4'he == value ? cfi_info_14_cfi_idx : _GEN_694;  assign _GEN_700 = 4'he == value ? cfi_info_14_cfi_type : _GEN_695;  assign _GEN_701 = 4'hf == value ? cfi_info_15_executed : _GEN_696;  assign _GEN_702 = 4'hf == value ? cfi_info_15_mispredicted : _GEN_697;  assign _GEN_703 = 4'hf == value ? cfi_info_15_taken : _GEN_698;  assign _GEN_705 = 4'hf == value ? cfi_info_15_cfi_type : _GEN_700;  assign _T_1801 = _GEN_703 == 1'h0;  assign _T_1802 = _T_1800 & _T_1801;  assign _T_1803 = ram_bim_info_value__T_1746_data == 2'h3;  assign _T_1804 = _T_1803 & _GEN_703;  assign _T_1805 = _T_1802 | _T_1804;  assign _T_1806 = _GEN_705 == 3'h1;  assign _T_1807 = _T_1806 & _GEN_702;  assign _T_1808 = _GEN_702 == 1'h0;  assign _T_1809 = _T_1808 & _GEN_701;  assign _T_1810 = _T_1805 == 1'h0;  assign _T_1811 = _T_1809 & _T_1810;  assign _T_1812 = _T_1807 | _T_1811;  assign _GEN_718 = ram_fetch_pc__T_1746_data;  assign _T_1816 = io_flush_valid ? {{12'd0}, io_com_ftq_idx} : io_brinfo_ftq_idx;  assign _T_1873 = io_get_ftq_pc_ftq_idx + 4'h1;  assign _T_1908 = io_flush_bits_flush_typ[0];  assign _T_1909 = _T_1908 == 1'h0;  assign _T_1911 = io_flush_bits_flush_typ == 3'h2;  assign _GEN_823 = {{34'd0}, io_flush_bits_pc_lob};  assign com_pc = _T_1944 + _GEN_823;  assign com_pc_plus4 = com_pc + 40'h4;  assign _T_1940 = ~ io_com_fetch_pc;  assign _T_1941 = _T_1940 | 40'h3f;  assign _T_1951 = _T_1950 == io_flush_bits_ftq_idx;  assign _T_1953 = _T_1951 | reset;  assign _T_1954 = _T_1953 == 1'h0;  assign io_enq_ready = full == 1'h0;  assign io_enq_idx = value_1;  assign io_get_ftq_pc_fetch_pc = ram_fetch_pc__T_1845_data;  assign io_get_ftq_pc_next_val = _T_1873 != value_1;  assign io_get_ftq_pc_next_pc = ram_fetch_pc__T_1875_data;  assign io_restore_history_valid = _T_1480 | io_flush_valid;  assign io_restore_history_bits_history = io_restore_history_valid ? ram_history__T_1818_data : 90'h0;  assign io_restore_history_bits_taken = io_brinfo_valid & io_brinfo_taken;  assign io_take_pc_valid = io_flush_valid & _T_1909;  assign io_take_pc_bits_addr = _T_1911 ? com_pc : com_pc_plus4;  assign io_com_fetch_pc = ram_fetch_pc__T_1913_data;  assign io_bim_update_valid = _T_1383 ? _T_1812 : 1'h0;  assign io_bim_update_bits_entry_idx = ram_bim_info_entry_idx__T_1746_data;  assign io_bim_update_bits_cfi_idx = 4'hf == value ? cfi_info_15_cfi_idx : _GEN_699;  assign io_bim_update_bits_cntr_value = ram_bim_info_value__T_1746_data;  assign io_bim_update_bits_mispredicted = 4'hf == value ? cfi_info_15_mispredicted : _GEN_697;  assign io_bim_update_bits_taken = 4'hf == value ? cfi_info_15_taken : _GEN_698;  assign io_bpd_update_valid = value != commit_ptr;  assign io_bpd_update_bits_fetch_pc = _GEN_718[38:0];  assign io_bpd_update_bits_history = ram_history__T_1746_data;  assign io_bpd_update_bits_mispredict = 4'hf == value ? cfi_info_15_mispredicted : _GEN_697;  assign io_bpd_update_bits_miss_cfi_idx = 4'hf == value ? cfi_info_15_cfi_idx : _GEN_699;  assign io_bpd_update_bits_taken = 4'hf == value ? cfi_info_15_taken : _GEN_698;  assign io_bpd_update_bits_info = ram_bpd_info__T_1746_data;`ifdef RANDOMIZE_GARBAGE_ASSIGN
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
`ifdef RANDOMIZE
  integer initvar;
  initial begin
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
  _RAND_0 = {2{`RANDOM}};
  `ifdef RANDOMIZE_MEM_INIT
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    ram_fetch_pc[initvar] = _RAND_0[39:0];
  `endif  _RAND_1 = {3{`RANDOM}};
  `ifdef RANDOMIZE_MEM_INIT
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    ram_history[initvar] = _RAND_1[89:0];
  `endif  _RAND_2 = {1{`RANDOM}};
  `ifdef RANDOMIZE_MEM_INIT
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    ram_bim_info_value[initvar] = _RAND_2[1:0];
  `endif  _RAND_3 = {1{`RANDOM}};
  `ifdef RANDOMIZE_MEM_INIT
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    ram_bim_info_entry_idx[initvar] = _RAND_3[9:0];
  `endif  _RAND_4 = {5{`RANDOM}};
  `ifdef RANDOMIZE_MEM_INIT
  for (initvar = 0; initvar < 16; initvar = initvar+1)
    ram_bpd_info[initvar] = _RAND_4[141:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_5 = {1{`RANDOM}};
  value = _RAND_5[3:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_6 = {1{`RANDOM}};
  value_1 = _RAND_6[3:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_7 = {1{`RANDOM}};
  maybe_full = _RAND_7[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_8 = {1{`RANDOM}};
  commit_ptr = _RAND_8[3:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_9 = {1{`RANDOM}};
  cfi_info_0_executed = _RAND_9[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_10 = {1{`RANDOM}};
  cfi_info_0_mispredicted = _RAND_10[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_11 = {1{`RANDOM}};
  cfi_info_0_taken = _RAND_11[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_12 = {1{`RANDOM}};
  cfi_info_0_cfi_idx = _RAND_12[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_13 = {1{`RANDOM}};
  cfi_info_0_cfi_type = _RAND_13[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_14 = {1{`RANDOM}};
  cfi_info_1_executed = _RAND_14[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_15 = {1{`RANDOM}};
  cfi_info_1_mispredicted = _RAND_15[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_16 = {1{`RANDOM}};
  cfi_info_1_taken = _RAND_16[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_17 = {1{`RANDOM}};
  cfi_info_1_cfi_idx = _RAND_17[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_18 = {1{`RANDOM}};
  cfi_info_1_cfi_type = _RAND_18[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_19 = {1{`RANDOM}};
  cfi_info_2_executed = _RAND_19[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_20 = {1{`RANDOM}};
  cfi_info_2_mispredicted = _RAND_20[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_21 = {1{`RANDOM}};
  cfi_info_2_taken = _RAND_21[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_22 = {1{`RANDOM}};
  cfi_info_2_cfi_idx = _RAND_22[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_23 = {1{`RANDOM}};
  cfi_info_2_cfi_type = _RAND_23[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_24 = {1{`RANDOM}};
  cfi_info_3_executed = _RAND_24[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_25 = {1{`RANDOM}};
  cfi_info_3_mispredicted = _RAND_25[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_26 = {1{`RANDOM}};
  cfi_info_3_taken = _RAND_26[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_27 = {1{`RANDOM}};
  cfi_info_3_cfi_idx = _RAND_27[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_28 = {1{`RANDOM}};
  cfi_info_3_cfi_type = _RAND_28[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_29 = {1{`RANDOM}};
  cfi_info_4_executed = _RAND_29[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_30 = {1{`RANDOM}};
  cfi_info_4_mispredicted = _RAND_30[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_31 = {1{`RANDOM}};
  cfi_info_4_taken = _RAND_31[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_32 = {1{`RANDOM}};
  cfi_info_4_cfi_idx = _RAND_32[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_33 = {1{`RANDOM}};
  cfi_info_4_cfi_type = _RAND_33[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_34 = {1{`RANDOM}};
  cfi_info_5_executed = _RAND_34[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_35 = {1{`RANDOM}};
  cfi_info_5_mispredicted = _RAND_35[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_36 = {1{`RANDOM}};
  cfi_info_5_taken = _RAND_36[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_37 = {1{`RANDOM}};
  cfi_info_5_cfi_idx = _RAND_37[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_38 = {1{`RANDOM}};
  cfi_info_5_cfi_type = _RAND_38[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_39 = {1{`RANDOM}};
  cfi_info_6_executed = _RAND_39[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_40 = {1{`RANDOM}};
  cfi_info_6_mispredicted = _RAND_40[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_41 = {1{`RANDOM}};
  cfi_info_6_taken = _RAND_41[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_42 = {1{`RANDOM}};
  cfi_info_6_cfi_idx = _RAND_42[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_43 = {1{`RANDOM}};
  cfi_info_6_cfi_type = _RAND_43[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_44 = {1{`RANDOM}};
  cfi_info_7_executed = _RAND_44[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_45 = {1{`RANDOM}};
  cfi_info_7_mispredicted = _RAND_45[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_46 = {1{`RANDOM}};
  cfi_info_7_taken = _RAND_46[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_47 = {1{`RANDOM}};
  cfi_info_7_cfi_idx = _RAND_47[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_48 = {1{`RANDOM}};
  cfi_info_7_cfi_type = _RAND_48[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_49 = {1{`RANDOM}};
  cfi_info_8_executed = _RAND_49[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_50 = {1{`RANDOM}};
  cfi_info_8_mispredicted = _RAND_50[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_51 = {1{`RANDOM}};
  cfi_info_8_taken = _RAND_51[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_52 = {1{`RANDOM}};
  cfi_info_8_cfi_idx = _RAND_52[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_53 = {1{`RANDOM}};
  cfi_info_8_cfi_type = _RAND_53[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_54 = {1{`RANDOM}};
  cfi_info_9_executed = _RAND_54[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_55 = {1{`RANDOM}};
  cfi_info_9_mispredicted = _RAND_55[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_56 = {1{`RANDOM}};
  cfi_info_9_taken = _RAND_56[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_57 = {1{`RANDOM}};
  cfi_info_9_cfi_idx = _RAND_57[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_58 = {1{`RANDOM}};
  cfi_info_9_cfi_type = _RAND_58[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_59 = {1{`RANDOM}};
  cfi_info_10_executed = _RAND_59[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_60 = {1{`RANDOM}};
  cfi_info_10_mispredicted = _RAND_60[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_61 = {1{`RANDOM}};
  cfi_info_10_taken = _RAND_61[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_62 = {1{`RANDOM}};
  cfi_info_10_cfi_idx = _RAND_62[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_63 = {1{`RANDOM}};
  cfi_info_10_cfi_type = _RAND_63[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_64 = {1{`RANDOM}};
  cfi_info_11_executed = _RAND_64[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_65 = {1{`RANDOM}};
  cfi_info_11_mispredicted = _RAND_65[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_66 = {1{`RANDOM}};
  cfi_info_11_taken = _RAND_66[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_67 = {1{`RANDOM}};
  cfi_info_11_cfi_idx = _RAND_67[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_68 = {1{`RANDOM}};
  cfi_info_11_cfi_type = _RAND_68[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_69 = {1{`RANDOM}};
  cfi_info_12_executed = _RAND_69[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_70 = {1{`RANDOM}};
  cfi_info_12_mispredicted = _RAND_70[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_71 = {1{`RANDOM}};
  cfi_info_12_taken = _RAND_71[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_72 = {1{`RANDOM}};
  cfi_info_12_cfi_idx = _RAND_72[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_73 = {1{`RANDOM}};
  cfi_info_12_cfi_type = _RAND_73[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_74 = {1{`RANDOM}};
  cfi_info_13_executed = _RAND_74[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_75 = {1{`RANDOM}};
  cfi_info_13_mispredicted = _RAND_75[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_76 = {1{`RANDOM}};
  cfi_info_13_taken = _RAND_76[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_77 = {1{`RANDOM}};
  cfi_info_13_cfi_idx = _RAND_77[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_78 = {1{`RANDOM}};
  cfi_info_13_cfi_type = _RAND_78[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_79 = {1{`RANDOM}};
  cfi_info_14_executed = _RAND_79[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_80 = {1{`RANDOM}};
  cfi_info_14_mispredicted = _RAND_80[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_81 = {1{`RANDOM}};
  cfi_info_14_taken = _RAND_81[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_82 = {1{`RANDOM}};
  cfi_info_14_cfi_idx = _RAND_82[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_83 = {1{`RANDOM}};
  cfi_info_14_cfi_type = _RAND_83[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_84 = {1{`RANDOM}};
  cfi_info_15_executed = _RAND_84[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_85 = {1{`RANDOM}};
  cfi_info_15_mispredicted = _RAND_85[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_86 = {1{`RANDOM}};
  cfi_info_15_taken = _RAND_86[0:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_87 = {1{`RANDOM}};
  cfi_info_15_cfi_idx = _RAND_87[1:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_88 = {1{`RANDOM}};
  cfi_info_15_cfi_type = _RAND_88[2:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_89 = {2{`RANDOM}};
  _T_1944 = _RAND_89[39:0];
  `endif  `ifdef RANDOMIZE_REG_INIT
  _RAND_90 = {1{`RANDOM}};
  _T_1950 = _RAND_90[3:0];
  `endif  end
`endif  always @(posedge clock) begin
    if(ram_fetch_pc__T_1386_en & ram_fetch_pc__T_1386_mask) begin
      ram_fetch_pc[ram_fetch_pc__T_1386_addr] <= ram_fetch_pc__T_1386_data;    end
    if(ram_history__T_1386_en & ram_history__T_1386_mask) begin
      ram_history[ram_history__T_1386_addr] <= ram_history__T_1386_data;    end
    if(ram_bim_info_value__T_1386_en & ram_bim_info_value__T_1386_mask) begin
      ram_bim_info_value[ram_bim_info_value__T_1386_addr] <= ram_bim_info_value__T_1386_data;    end
    if(ram_bim_info_entry_idx__T_1386_en & ram_bim_info_entry_idx__T_1386_mask) begin
      ram_bim_info_entry_idx[ram_bim_info_entry_idx__T_1386_addr] <= ram_bim_info_entry_idx__T_1386_data;    end
    if(ram_bpd_info__T_1386_en & ram_bpd_info__T_1386_mask) begin
      ram_bpd_info[ram_bpd_info__T_1386_addr] <= ram_bpd_info__T_1386_data;    end
    if (reset) begin
      value <= 4'h0;
    end else begin
      if (_T_1383) begin
        value <= _T_1474;
      end
    end
    if (reset) begin
      value_1 <= 4'h0;
    end else begin
      if (_T_1480) begin
        value_1 <= _T_1483;
      end else begin
        if (io_flush_valid) begin
          value_1 <= _T_1478;
        end else begin
          if (_T_1380) begin
            value_1 <= _T_1471;
          end
        end
      end
    end
    if (reset) begin
      maybe_full <= 1'h0;
    end else begin
      if (_T_1480) begin
        maybe_full <= _T_1484;
      end else begin
        if (_T_1475) begin
          maybe_full <= _T_1380;
        end
      end
    end
    if (reset) begin
      commit_ptr <= 4'h0;
    end else begin
      if (io_deq_valid) begin
        commit_ptr <= io_deq_bits;
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'h0 == value_1) begin
            cfi_info_0_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h0 == _T_1512) begin
            cfi_info_0_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'h0 == value_1) begin
                cfi_info_0_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h0 == value_1) begin
              cfi_info_0_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h0 == value_1) begin
          cfi_info_0_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h0 == _T_1512) begin
          cfi_info_0_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'h0 == value_1) begin
              cfi_info_0_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h0 == value_1) begin
            cfi_info_0_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h0 == value_1) begin
          cfi_info_0_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h0 == _T_1512) begin
          cfi_info_0_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'h0 == value_1) begin
              cfi_info_0_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h0 == _T_1512) begin
            cfi_info_0_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'h0 == value_1) begin
                cfi_info_0_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h0 == value_1) begin
              cfi_info_0_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h0 == value_1) begin
          cfi_info_0_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h0 == _T_1512) begin
          cfi_info_0_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'h0 == value_1) begin
              cfi_info_0_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h0 == value_1) begin
            cfi_info_0_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h0 == value_1) begin
          cfi_info_0_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h0 == _T_1512) begin
          cfi_info_0_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'h0 == value_1) begin
              cfi_info_0_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h0 == value_1) begin
            cfi_info_0_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h0 == value_1) begin
          cfi_info_0_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'h1 == value_1) begin
            cfi_info_1_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h1 == _T_1512) begin
            cfi_info_1_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'h1 == value_1) begin
                cfi_info_1_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h1 == value_1) begin
              cfi_info_1_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h1 == value_1) begin
          cfi_info_1_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h1 == _T_1512) begin
          cfi_info_1_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'h1 == value_1) begin
              cfi_info_1_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h1 == value_1) begin
            cfi_info_1_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h1 == value_1) begin
          cfi_info_1_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h1 == _T_1512) begin
          cfi_info_1_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'h1 == value_1) begin
              cfi_info_1_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h1 == _T_1512) begin
            cfi_info_1_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'h1 == value_1) begin
                cfi_info_1_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h1 == value_1) begin
              cfi_info_1_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h1 == value_1) begin
          cfi_info_1_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h1 == _T_1512) begin
          cfi_info_1_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'h1 == value_1) begin
              cfi_info_1_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h1 == value_1) begin
            cfi_info_1_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h1 == value_1) begin
          cfi_info_1_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h1 == _T_1512) begin
          cfi_info_1_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'h1 == value_1) begin
              cfi_info_1_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h1 == value_1) begin
            cfi_info_1_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h1 == value_1) begin
          cfi_info_1_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'h2 == value_1) begin
            cfi_info_2_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h2 == _T_1512) begin
            cfi_info_2_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'h2 == value_1) begin
                cfi_info_2_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h2 == value_1) begin
              cfi_info_2_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h2 == value_1) begin
          cfi_info_2_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h2 == _T_1512) begin
          cfi_info_2_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'h2 == value_1) begin
              cfi_info_2_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h2 == value_1) begin
            cfi_info_2_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h2 == value_1) begin
          cfi_info_2_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h2 == _T_1512) begin
          cfi_info_2_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'h2 == value_1) begin
              cfi_info_2_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h2 == _T_1512) begin
            cfi_info_2_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'h2 == value_1) begin
                cfi_info_2_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h2 == value_1) begin
              cfi_info_2_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h2 == value_1) begin
          cfi_info_2_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h2 == _T_1512) begin
          cfi_info_2_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'h2 == value_1) begin
              cfi_info_2_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h2 == value_1) begin
            cfi_info_2_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h2 == value_1) begin
          cfi_info_2_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h2 == _T_1512) begin
          cfi_info_2_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'h2 == value_1) begin
              cfi_info_2_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h2 == value_1) begin
            cfi_info_2_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h2 == value_1) begin
          cfi_info_2_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'h3 == value_1) begin
            cfi_info_3_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h3 == _T_1512) begin
            cfi_info_3_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'h3 == value_1) begin
                cfi_info_3_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h3 == value_1) begin
              cfi_info_3_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h3 == value_1) begin
          cfi_info_3_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h3 == _T_1512) begin
          cfi_info_3_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'h3 == value_1) begin
              cfi_info_3_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h3 == value_1) begin
            cfi_info_3_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h3 == value_1) begin
          cfi_info_3_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h3 == _T_1512) begin
          cfi_info_3_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'h3 == value_1) begin
              cfi_info_3_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h3 == _T_1512) begin
            cfi_info_3_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'h3 == value_1) begin
                cfi_info_3_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h3 == value_1) begin
              cfi_info_3_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h3 == value_1) begin
          cfi_info_3_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h3 == _T_1512) begin
          cfi_info_3_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'h3 == value_1) begin
              cfi_info_3_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h3 == value_1) begin
            cfi_info_3_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h3 == value_1) begin
          cfi_info_3_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h3 == _T_1512) begin
          cfi_info_3_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'h3 == value_1) begin
              cfi_info_3_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h3 == value_1) begin
            cfi_info_3_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h3 == value_1) begin
          cfi_info_3_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'h4 == value_1) begin
            cfi_info_4_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h4 == _T_1512) begin
            cfi_info_4_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'h4 == value_1) begin
                cfi_info_4_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h4 == value_1) begin
              cfi_info_4_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h4 == value_1) begin
          cfi_info_4_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h4 == _T_1512) begin
          cfi_info_4_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'h4 == value_1) begin
              cfi_info_4_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h4 == value_1) begin
            cfi_info_4_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h4 == value_1) begin
          cfi_info_4_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h4 == _T_1512) begin
          cfi_info_4_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'h4 == value_1) begin
              cfi_info_4_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h4 == _T_1512) begin
            cfi_info_4_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'h4 == value_1) begin
                cfi_info_4_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h4 == value_1) begin
              cfi_info_4_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h4 == value_1) begin
          cfi_info_4_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h4 == _T_1512) begin
          cfi_info_4_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'h4 == value_1) begin
              cfi_info_4_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h4 == value_1) begin
            cfi_info_4_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h4 == value_1) begin
          cfi_info_4_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h4 == _T_1512) begin
          cfi_info_4_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'h4 == value_1) begin
              cfi_info_4_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h4 == value_1) begin
            cfi_info_4_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h4 == value_1) begin
          cfi_info_4_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'h5 == value_1) begin
            cfi_info_5_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h5 == _T_1512) begin
            cfi_info_5_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'h5 == value_1) begin
                cfi_info_5_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h5 == value_1) begin
              cfi_info_5_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h5 == value_1) begin
          cfi_info_5_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h5 == _T_1512) begin
          cfi_info_5_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'h5 == value_1) begin
              cfi_info_5_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h5 == value_1) begin
            cfi_info_5_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h5 == value_1) begin
          cfi_info_5_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h5 == _T_1512) begin
          cfi_info_5_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'h5 == value_1) begin
              cfi_info_5_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h5 == _T_1512) begin
            cfi_info_5_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'h5 == value_1) begin
                cfi_info_5_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h5 == value_1) begin
              cfi_info_5_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h5 == value_1) begin
          cfi_info_5_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h5 == _T_1512) begin
          cfi_info_5_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'h5 == value_1) begin
              cfi_info_5_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h5 == value_1) begin
            cfi_info_5_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h5 == value_1) begin
          cfi_info_5_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h5 == _T_1512) begin
          cfi_info_5_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'h5 == value_1) begin
              cfi_info_5_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h5 == value_1) begin
            cfi_info_5_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h5 == value_1) begin
          cfi_info_5_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'h6 == value_1) begin
            cfi_info_6_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h6 == _T_1512) begin
            cfi_info_6_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'h6 == value_1) begin
                cfi_info_6_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h6 == value_1) begin
              cfi_info_6_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h6 == value_1) begin
          cfi_info_6_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h6 == _T_1512) begin
          cfi_info_6_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'h6 == value_1) begin
              cfi_info_6_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h6 == value_1) begin
            cfi_info_6_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h6 == value_1) begin
          cfi_info_6_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h6 == _T_1512) begin
          cfi_info_6_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'h6 == value_1) begin
              cfi_info_6_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h6 == _T_1512) begin
            cfi_info_6_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'h6 == value_1) begin
                cfi_info_6_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h6 == value_1) begin
              cfi_info_6_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h6 == value_1) begin
          cfi_info_6_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h6 == _T_1512) begin
          cfi_info_6_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'h6 == value_1) begin
              cfi_info_6_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h6 == value_1) begin
            cfi_info_6_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h6 == value_1) begin
          cfi_info_6_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h6 == _T_1512) begin
          cfi_info_6_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'h6 == value_1) begin
              cfi_info_6_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h6 == value_1) begin
            cfi_info_6_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h6 == value_1) begin
          cfi_info_6_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'h7 == value_1) begin
            cfi_info_7_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h7 == _T_1512) begin
            cfi_info_7_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'h7 == value_1) begin
                cfi_info_7_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h7 == value_1) begin
              cfi_info_7_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h7 == value_1) begin
          cfi_info_7_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h7 == _T_1512) begin
          cfi_info_7_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'h7 == value_1) begin
              cfi_info_7_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h7 == value_1) begin
            cfi_info_7_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h7 == value_1) begin
          cfi_info_7_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h7 == _T_1512) begin
          cfi_info_7_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'h7 == value_1) begin
              cfi_info_7_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h7 == _T_1512) begin
            cfi_info_7_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'h7 == value_1) begin
                cfi_info_7_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h7 == value_1) begin
              cfi_info_7_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h7 == value_1) begin
          cfi_info_7_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h7 == _T_1512) begin
          cfi_info_7_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'h7 == value_1) begin
              cfi_info_7_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h7 == value_1) begin
            cfi_info_7_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h7 == value_1) begin
          cfi_info_7_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h7 == _T_1512) begin
          cfi_info_7_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'h7 == value_1) begin
              cfi_info_7_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h7 == value_1) begin
            cfi_info_7_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h7 == value_1) begin
          cfi_info_7_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'h8 == value_1) begin
            cfi_info_8_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h8 == _T_1512) begin
            cfi_info_8_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'h8 == value_1) begin
                cfi_info_8_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h8 == value_1) begin
              cfi_info_8_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h8 == value_1) begin
          cfi_info_8_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h8 == _T_1512) begin
          cfi_info_8_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'h8 == value_1) begin
              cfi_info_8_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h8 == value_1) begin
            cfi_info_8_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h8 == value_1) begin
          cfi_info_8_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h8 == _T_1512) begin
          cfi_info_8_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'h8 == value_1) begin
              cfi_info_8_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h8 == _T_1512) begin
            cfi_info_8_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'h8 == value_1) begin
                cfi_info_8_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h8 == value_1) begin
              cfi_info_8_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h8 == value_1) begin
          cfi_info_8_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h8 == _T_1512) begin
          cfi_info_8_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'h8 == value_1) begin
              cfi_info_8_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h8 == value_1) begin
            cfi_info_8_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h8 == value_1) begin
          cfi_info_8_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h8 == _T_1512) begin
          cfi_info_8_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'h8 == value_1) begin
              cfi_info_8_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h8 == value_1) begin
            cfi_info_8_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h8 == value_1) begin
          cfi_info_8_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'h9 == value_1) begin
            cfi_info_9_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h9 == _T_1512) begin
            cfi_info_9_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'h9 == value_1) begin
                cfi_info_9_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h9 == value_1) begin
              cfi_info_9_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h9 == value_1) begin
          cfi_info_9_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h9 == _T_1512) begin
          cfi_info_9_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'h9 == value_1) begin
              cfi_info_9_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h9 == value_1) begin
            cfi_info_9_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h9 == value_1) begin
          cfi_info_9_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h9 == _T_1512) begin
          cfi_info_9_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'h9 == value_1) begin
              cfi_info_9_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'h9 == _T_1512) begin
            cfi_info_9_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'h9 == value_1) begin
                cfi_info_9_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'h9 == value_1) begin
              cfi_info_9_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h9 == value_1) begin
          cfi_info_9_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h9 == _T_1512) begin
          cfi_info_9_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'h9 == value_1) begin
              cfi_info_9_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h9 == value_1) begin
            cfi_info_9_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h9 == value_1) begin
          cfi_info_9_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'h9 == _T_1512) begin
          cfi_info_9_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'h9 == value_1) begin
              cfi_info_9_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'h9 == value_1) begin
            cfi_info_9_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'h9 == value_1) begin
          cfi_info_9_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'ha == value_1) begin
            cfi_info_10_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'ha == _T_1512) begin
            cfi_info_10_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'ha == value_1) begin
                cfi_info_10_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'ha == value_1) begin
              cfi_info_10_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'ha == value_1) begin
          cfi_info_10_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'ha == _T_1512) begin
          cfi_info_10_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'ha == value_1) begin
              cfi_info_10_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'ha == value_1) begin
            cfi_info_10_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'ha == value_1) begin
          cfi_info_10_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'ha == _T_1512) begin
          cfi_info_10_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'ha == value_1) begin
              cfi_info_10_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'ha == _T_1512) begin
            cfi_info_10_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'ha == value_1) begin
                cfi_info_10_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'ha == value_1) begin
              cfi_info_10_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'ha == value_1) begin
          cfi_info_10_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'ha == _T_1512) begin
          cfi_info_10_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'ha == value_1) begin
              cfi_info_10_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'ha == value_1) begin
            cfi_info_10_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'ha == value_1) begin
          cfi_info_10_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'ha == _T_1512) begin
          cfi_info_10_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'ha == value_1) begin
              cfi_info_10_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'ha == value_1) begin
            cfi_info_10_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'ha == value_1) begin
          cfi_info_10_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'hb == value_1) begin
            cfi_info_11_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'hb == _T_1512) begin
            cfi_info_11_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'hb == value_1) begin
                cfi_info_11_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'hb == value_1) begin
              cfi_info_11_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hb == value_1) begin
          cfi_info_11_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hb == _T_1512) begin
          cfi_info_11_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'hb == value_1) begin
              cfi_info_11_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'hb == value_1) begin
            cfi_info_11_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hb == value_1) begin
          cfi_info_11_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hb == _T_1512) begin
          cfi_info_11_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'hb == value_1) begin
              cfi_info_11_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'hb == _T_1512) begin
            cfi_info_11_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'hb == value_1) begin
                cfi_info_11_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'hb == value_1) begin
              cfi_info_11_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hb == value_1) begin
          cfi_info_11_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hb == _T_1512) begin
          cfi_info_11_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'hb == value_1) begin
              cfi_info_11_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'hb == value_1) begin
            cfi_info_11_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hb == value_1) begin
          cfi_info_11_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hb == _T_1512) begin
          cfi_info_11_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'hb == value_1) begin
              cfi_info_11_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'hb == value_1) begin
            cfi_info_11_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hb == value_1) begin
          cfi_info_11_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'hc == value_1) begin
            cfi_info_12_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'hc == _T_1512) begin
            cfi_info_12_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'hc == value_1) begin
                cfi_info_12_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'hc == value_1) begin
              cfi_info_12_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hc == value_1) begin
          cfi_info_12_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hc == _T_1512) begin
          cfi_info_12_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'hc == value_1) begin
              cfi_info_12_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'hc == value_1) begin
            cfi_info_12_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hc == value_1) begin
          cfi_info_12_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hc == _T_1512) begin
          cfi_info_12_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'hc == value_1) begin
              cfi_info_12_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'hc == _T_1512) begin
            cfi_info_12_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'hc == value_1) begin
                cfi_info_12_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'hc == value_1) begin
              cfi_info_12_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hc == value_1) begin
          cfi_info_12_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hc == _T_1512) begin
          cfi_info_12_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'hc == value_1) begin
              cfi_info_12_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'hc == value_1) begin
            cfi_info_12_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hc == value_1) begin
          cfi_info_12_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hc == _T_1512) begin
          cfi_info_12_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'hc == value_1) begin
              cfi_info_12_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'hc == value_1) begin
            cfi_info_12_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hc == value_1) begin
          cfi_info_12_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'hd == value_1) begin
            cfi_info_13_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'hd == _T_1512) begin
            cfi_info_13_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'hd == value_1) begin
                cfi_info_13_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'hd == value_1) begin
              cfi_info_13_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hd == value_1) begin
          cfi_info_13_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hd == _T_1512) begin
          cfi_info_13_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'hd == value_1) begin
              cfi_info_13_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'hd == value_1) begin
            cfi_info_13_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hd == value_1) begin
          cfi_info_13_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hd == _T_1512) begin
          cfi_info_13_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'hd == value_1) begin
              cfi_info_13_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'hd == _T_1512) begin
            cfi_info_13_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'hd == value_1) begin
                cfi_info_13_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'hd == value_1) begin
              cfi_info_13_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hd == value_1) begin
          cfi_info_13_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hd == _T_1512) begin
          cfi_info_13_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'hd == value_1) begin
              cfi_info_13_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'hd == value_1) begin
            cfi_info_13_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hd == value_1) begin
          cfi_info_13_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hd == _T_1512) begin
          cfi_info_13_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'hd == value_1) begin
              cfi_info_13_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'hd == value_1) begin
            cfi_info_13_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hd == value_1) begin
          cfi_info_13_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'he == value_1) begin
            cfi_info_14_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'he == _T_1512) begin
            cfi_info_14_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'he == value_1) begin
                cfi_info_14_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'he == value_1) begin
              cfi_info_14_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'he == value_1) begin
          cfi_info_14_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'he == _T_1512) begin
          cfi_info_14_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'he == value_1) begin
              cfi_info_14_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'he == value_1) begin
            cfi_info_14_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'he == value_1) begin
          cfi_info_14_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'he == _T_1512) begin
          cfi_info_14_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'he == value_1) begin
              cfi_info_14_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'he == _T_1512) begin
            cfi_info_14_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'he == value_1) begin
                cfi_info_14_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'he == value_1) begin
              cfi_info_14_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'he == value_1) begin
          cfi_info_14_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'he == _T_1512) begin
          cfi_info_14_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'he == value_1) begin
              cfi_info_14_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'he == value_1) begin
            cfi_info_14_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'he == value_1) begin
          cfi_info_14_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'he == _T_1512) begin
          cfi_info_14_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'he == value_1) begin
              cfi_info_14_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'he == value_1) begin
            cfi_info_14_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'he == value_1) begin
          cfi_info_14_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (_T_1380) begin
          if (4'hf == value_1) begin
            cfi_info_15_executed <= 1'h0;
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'hf == _T_1512) begin
            cfi_info_15_executed <= 1'h1;
          end else begin
            if (_T_1380) begin
              if (4'hf == value_1) begin
                cfi_info_15_executed <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'hf == value_1) begin
              cfi_info_15_executed <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hf == value_1) begin
          cfi_info_15_executed <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hf == _T_1512) begin
          cfi_info_15_mispredicted <= 1'h1;
        end else begin
          if (_T_1380) begin
            if (4'hf == value_1) begin
              cfi_info_15_mispredicted <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'hf == value_1) begin
            cfi_info_15_mispredicted <= 1'h0;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hf == value_1) begin
          cfi_info_15_mispredicted <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hf == _T_1512) begin
          cfi_info_15_taken <= io_brinfo_taken;
        end else begin
          if (_T_1380) begin
            if (4'hf == value_1) begin
              cfi_info_15_taken <= 1'h0;
            end
          end
        end
      end else begin
        if (_T_1689) begin
          if (4'hf == _T_1512) begin
            cfi_info_15_taken <= io_brinfo_taken;
          end else begin
            if (_T_1380) begin
              if (4'hf == value_1) begin
                cfi_info_15_taken <= 1'h0;
              end
            end
          end
        end else begin
          if (_T_1380) begin
            if (4'hf == value_1) begin
              cfi_info_15_taken <= 1'h0;
            end
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hf == value_1) begin
          cfi_info_15_taken <= 1'h0;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hf == _T_1512) begin
          cfi_info_15_cfi_idx <= _T_1569;
        end else begin
          if (_T_1380) begin
            if (4'hf == value_1) begin
              cfi_info_15_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'hf == value_1) begin
            cfi_info_15_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hf == value_1) begin
          cfi_info_15_cfi_idx <= io_enq_bits_bim_info_cfi_idx;
        end
      end
    end
    if (io_brinfo_valid) begin
      if (_T_1574) begin
        if (4'hf == _T_1512) begin
          cfi_info_15_cfi_type <= io_brinfo_cfi_type;
        end else begin
          if (_T_1380) begin
            if (4'hf == value_1) begin
              cfi_info_15_cfi_type <= _T_1454_cfi_type;
            end
          end
        end
      end else begin
        if (_T_1380) begin
          if (4'hf == value_1) begin
            cfi_info_15_cfi_type <= _T_1454_cfi_type;
          end
        end
      end
    end else begin
      if (_T_1380) begin
        if (4'hf == value_1) begin
          cfi_info_15_cfi_type <= _T_1454_cfi_type;
        end
      end
    end
    _T_1944 <= ~ _T_1941;
    _T_1950 <= io_com_ftq_idx;
    `ifndef SYNTHESIS
    `ifdef PRINTF_COND
      if (`PRINTF_COND) begin
    `endif
        if (_T_1954) begin
          $fwrite(32'h80000002,"Assertion failed: [ftq] this code depends on this assumption\n    at fetchtargetqueue.scala:302 assert (RegNext(io.com_ftq_idx) === io.flush.bits.ftq_idx, \"[ftq] this code depends on this assumption\")\n");        end
    `ifdef PRINTF_COND
      end
    `endif
    `endif    `ifndef SYNTHESIS
    `ifdef STOP_COND
      if (`STOP_COND) begin
    `endif
        if (_T_1954) begin
          $fatal;        end
    `ifdef STOP_COND
      end
    `endif
    `endif  end
endmodule