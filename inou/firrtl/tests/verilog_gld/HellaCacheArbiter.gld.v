module HellaCacheArbiter(
  input         clock,
  input         reset,
  output        io_requestor_0_req_ready,
  input         io_requestor_0_req_valid,
  input  [39:0] io_requestor_0_req_bits_addr,
  input  [6:0]  io_requestor_0_req_bits_tag,
  input  [4:0]  io_requestor_0_req_bits_cmd,
  input  [1:0]  io_requestor_0_req_bits_size,
  input         io_requestor_0_req_bits_signed,
  input  [1:0]  io_requestor_0_req_bits_dprv,
  input         io_requestor_0_req_bits_phys,
  input         io_requestor_0_req_bits_no_alloc,
  input         io_requestor_0_req_bits_no_xcpt,
  input  [63:0] io_requestor_0_req_bits_data,
  input  [7:0]  io_requestor_0_req_bits_mask,
  input         io_requestor_0_s1_kill,
  input  [63:0] io_requestor_0_s1_data_data,
  input  [7:0]  io_requestor_0_s1_data_mask,
  output        io_requestor_0_s2_nack,
  output        io_requestor_0_s2_nack_cause_raw,
  input         io_requestor_0_s2_kill,
  output        io_requestor_0_s2_uncached,
  output [31:0] io_requestor_0_s2_paddr,
  output        io_requestor_0_resp_valid,
  output [39:0] io_requestor_0_resp_bits_addr,
  output [6:0]  io_requestor_0_resp_bits_tag,
  output [4:0]  io_requestor_0_resp_bits_cmd,
  output [1:0]  io_requestor_0_resp_bits_size,
  output        io_requestor_0_resp_bits_signed,
  output [1:0]  io_requestor_0_resp_bits_dprv,
  output [63:0] io_requestor_0_resp_bits_data,
  output [7:0]  io_requestor_0_resp_bits_mask,
  output        io_requestor_0_resp_bits_replay,
  output        io_requestor_0_resp_bits_has_data,
  output [63:0] io_requestor_0_resp_bits_data_word_bypass,
  output [63:0] io_requestor_0_resp_bits_data_raw,
  output [63:0] io_requestor_0_resp_bits_store_data,
  output        io_requestor_0_replay_next,
  output        io_requestor_0_s2_xcpt_ma_ld,
  output        io_requestor_0_s2_xcpt_ma_st,
  output        io_requestor_0_s2_xcpt_pf_ld,
  output        io_requestor_0_s2_xcpt_pf_st,
  output        io_requestor_0_s2_xcpt_ae_ld,
  output        io_requestor_0_s2_xcpt_ae_st,
  output        io_requestor_0_ordered,
  output        io_requestor_0_perf_acquire,
  output        io_requestor_0_perf_release,
  output        io_requestor_0_perf_grant,
  output        io_requestor_0_perf_tlbMiss,
  output        io_requestor_0_perf_blocked,
  output        io_requestor_0_perf_canAcceptStoreThenLoad,
  output        io_requestor_0_perf_canAcceptStoreThenRMW,
  output        io_requestor_0_perf_canAcceptLoadThenLoad,
  output        io_requestor_0_perf_storeBufferEmptyAfterLoad,
  output        io_requestor_0_perf_storeBufferEmptyAfterStore,
  input         io_requestor_0_keep_clock_enabled,
  output        io_requestor_0_clock_enabled,
  input         io_mem_req_ready,
  output        io_mem_req_valid,
  output [39:0] io_mem_req_bits_addr,
  output [6:0]  io_mem_req_bits_tag,
  output [4:0]  io_mem_req_bits_cmd,
  output [1:0]  io_mem_req_bits_size,
  output        io_mem_req_bits_signed,
  output [1:0]  io_mem_req_bits_dprv,
  output        io_mem_req_bits_phys,
  output        io_mem_req_bits_no_alloc,
  output        io_mem_req_bits_no_xcpt,
  output [63:0] io_mem_req_bits_data,
  output [7:0]  io_mem_req_bits_mask,
  output        io_mem_s1_kill,
  output [63:0] io_mem_s1_data_data,
  output [7:0]  io_mem_s1_data_mask,
  input         io_mem_s2_nack,
  input         io_mem_s2_nack_cause_raw,
  output        io_mem_s2_kill,
  input         io_mem_s2_uncached,
  input  [31:0] io_mem_s2_paddr,
  input         io_mem_resp_valid,
  input  [39:0] io_mem_resp_bits_addr,
  input  [6:0]  io_mem_resp_bits_tag,
  input  [4:0]  io_mem_resp_bits_cmd,
  input  [1:0]  io_mem_resp_bits_size,
  input         io_mem_resp_bits_signed,
  input  [1:0]  io_mem_resp_bits_dprv,
  input  [63:0] io_mem_resp_bits_data,
  input  [7:0]  io_mem_resp_bits_mask,
  input         io_mem_resp_bits_replay,
  input         io_mem_resp_bits_has_data,
  input  [63:0] io_mem_resp_bits_data_word_bypass,
  input  [63:0] io_mem_resp_bits_data_raw,
  input  [63:0] io_mem_resp_bits_store_data,
  input         io_mem_replay_next,
  input         io_mem_s2_xcpt_ma_ld,
  input         io_mem_s2_xcpt_ma_st,
  input         io_mem_s2_xcpt_pf_ld,
  input         io_mem_s2_xcpt_pf_st,
  input         io_mem_s2_xcpt_ae_ld,
  input         io_mem_s2_xcpt_ae_st,
  input         io_mem_ordered,
  input         io_mem_perf_acquire,
  input         io_mem_perf_release,
  input         io_mem_perf_grant,
  input         io_mem_perf_tlbMiss,
  input         io_mem_perf_blocked,
  input         io_mem_perf_canAcceptStoreThenLoad,
  input         io_mem_perf_canAcceptStoreThenRMW,
  input         io_mem_perf_canAcceptLoadThenLoad,
  input         io_mem_perf_storeBufferEmptyAfterLoad,
  input         io_mem_perf_storeBufferEmptyAfterStore,
  output        io_mem_keep_clock_enabled,
  input         io_mem_clock_enabled
);
  assign io_requestor_0_req_ready = io_mem_req_ready; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_s2_nack = io_mem_s2_nack; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_s2_nack_cause_raw = io_mem_s2_nack_cause_raw; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_s2_uncached = io_mem_s2_uncached; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_s2_paddr = io_mem_s2_paddr; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_resp_valid = io_mem_resp_valid; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_resp_bits_addr = io_mem_resp_bits_addr; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_resp_bits_tag = io_mem_resp_bits_tag; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_resp_bits_cmd = io_mem_resp_bits_cmd; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_resp_bits_size = io_mem_resp_bits_size; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_resp_bits_signed = io_mem_resp_bits_signed; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_resp_bits_dprv = io_mem_resp_bits_dprv; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_resp_bits_data = io_mem_resp_bits_data; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_resp_bits_mask = io_mem_resp_bits_mask; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_resp_bits_replay = io_mem_resp_bits_replay; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_resp_bits_has_data = io_mem_resp_bits_has_data; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_resp_bits_data_word_bypass = io_mem_resp_bits_data_word_bypass; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_resp_bits_data_raw = io_mem_resp_bits_data_raw; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_resp_bits_store_data = io_mem_resp_bits_store_data; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_replay_next = io_mem_replay_next; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_s2_xcpt_ma_ld = io_mem_s2_xcpt_ma_ld; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_s2_xcpt_ma_st = io_mem_s2_xcpt_ma_st; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_s2_xcpt_pf_ld = io_mem_s2_xcpt_pf_ld; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_s2_xcpt_pf_st = io_mem_s2_xcpt_pf_st; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_s2_xcpt_ae_ld = io_mem_s2_xcpt_ae_ld; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_s2_xcpt_ae_st = io_mem_s2_xcpt_ae_st; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_ordered = io_mem_ordered; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_perf_acquire = io_mem_perf_acquire; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_perf_release = io_mem_perf_release; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_perf_grant = io_mem_perf_grant; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_perf_tlbMiss = io_mem_perf_tlbMiss; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_perf_blocked = io_mem_perf_blocked; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_perf_canAcceptStoreThenLoad = io_mem_perf_canAcceptStoreThenLoad; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_perf_canAcceptStoreThenRMW = io_mem_perf_canAcceptStoreThenRMW; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_perf_canAcceptLoadThenLoad = io_mem_perf_canAcceptLoadThenLoad; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_perf_storeBufferEmptyAfterLoad = io_mem_perf_storeBufferEmptyAfterLoad; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_perf_storeBufferEmptyAfterStore = io_mem_perf_storeBufferEmptyAfterStore; // @[HellaCacheArbiter.scala 17:12]
  assign io_requestor_0_clock_enabled = io_mem_clock_enabled; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_req_valid = io_requestor_0_req_valid; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_req_bits_addr = io_requestor_0_req_bits_addr; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_req_bits_tag = io_requestor_0_req_bits_tag; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_req_bits_cmd = io_requestor_0_req_bits_cmd; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_req_bits_size = io_requestor_0_req_bits_size; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_req_bits_signed = io_requestor_0_req_bits_signed; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_req_bits_dprv = io_requestor_0_req_bits_dprv; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_req_bits_phys = io_requestor_0_req_bits_phys; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_req_bits_no_alloc = io_requestor_0_req_bits_no_alloc; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_req_bits_no_xcpt = io_requestor_0_req_bits_no_xcpt; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_req_bits_data = io_requestor_0_req_bits_data; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_req_bits_mask = io_requestor_0_req_bits_mask; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_s1_kill = io_requestor_0_s1_kill; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_s1_data_data = io_requestor_0_s1_data_data; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_s1_data_mask = io_requestor_0_s1_data_mask; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_s2_kill = io_requestor_0_s2_kill; // @[HellaCacheArbiter.scala 17:12]
  assign io_mem_keep_clock_enabled = io_requestor_0_keep_clock_enabled; // @[HellaCacheArbiter.scala 17:12]
endmodule
