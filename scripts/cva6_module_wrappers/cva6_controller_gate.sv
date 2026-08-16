// One-line instantiation top for CVA6's `controller` (pipeline flush controller).
//
// Needs `CVA6Cfg` plus the `bp_resolve_t` type parameter.  CVA6 declares that
// struct as a `localparam type` INSIDE `cva6.sv`'s parameter list (cva6.sv:134),
// not in a package, so every standalone wrapper has to re-declare it -- same as
// `cva6_tlb_gate.sv` does for `pte_cva6_t` / `tlb_update_cva6_t`.
//
// Only 2 flops (`fence_active_q`, `fence_i_active_q`) against 277 lines of
// control logic, so this is a high logic-to-state ratio target.
module cva6_controller_gate
  import ariane_pkg::*;
#(
    parameter config_pkg::cva6_cfg_t CVA6Cfg = build_config_pkg::build_config(
        cva6_config_pkg::cva6_cfg
    )
) (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic v_i,
    output logic set_pc_commit_o,
    output logic flush_if_o,
    output logic flush_unissued_instr_o,
    output logic flush_id_o,
    output logic flush_ex_o,
    output logic flush_bp_o,
    output logic flush_icache_o,
    output logic flush_dcache_o,
    input  logic flush_dcache_ack_i,
    output logic flush_tlb_o,
    output logic flush_tlb_vvma_o,
    output logic flush_tlb_gvma_o,
    input  logic halt_csr_i,
    input  logic halt_acc_i,
    output logic halt_frontend_o,
    output logic halt_o,
    input  logic eret_i,
    input  logic ex_valid_i,
    input  logic set_debug_pc_i,
    // bp_resolve_t flattened to its fields so the top has plain `logic` ports.
    input  logic rb_valid_i,
    input  logic [CVA6Cfg.VLEN-1:0] rb_pc_i,
    input  logic [CVA6Cfg.VLEN-1:0] rb_target_address_i,
    input  logic rb_is_mispredict_i,
    input  logic rb_is_taken_i,
    input  logic [$bits(cf_t)-1:0] rb_cf_type_i,
    input  logic flush_csr_i,
    input  logic fence_i_i,
    input  logic fence_i,
    input  logic sfence_vma_i,
    input  logic hfence_vvma_i,
    input  logic hfence_gvma_i,
    input  logic flush_commit_i,
    input  logic flush_acc_i
);

  // Mirrors cva6.sv:134.
  typedef struct packed {
    logic                    valid;
    logic [CVA6Cfg.VLEN-1:0] pc;
    logic [CVA6Cfg.VLEN-1:0] target_address;
    logic                    is_mispredict;
    logic                    is_taken;
    cf_t                     cf_type;
  } bp_resolve_t;

  bp_resolve_t resolved_branch;

  always_comb begin
    resolved_branch                = '0;
    resolved_branch.valid          = rb_valid_i;
    resolved_branch.pc             = rb_pc_i;
    resolved_branch.target_address = rb_target_address_i;
    resolved_branch.is_mispredict  = rb_is_mispredict_i;
    resolved_branch.is_taken       = rb_is_taken_i;
    resolved_branch.cf_type        = cf_t'(rb_cf_type_i);
  end

  controller #(
      .CVA6Cfg     (CVA6Cfg),
      .bp_resolve_t(bp_resolve_t)
  ) dut (
      .clk_i                 (clk_i),
      .rst_ni                (rst_ni),
      .v_i                   (v_i),
      .set_pc_commit_o       (set_pc_commit_o),
      .flush_if_o            (flush_if_o),
      .flush_unissued_instr_o(flush_unissued_instr_o),
      .flush_id_o            (flush_id_o),
      .flush_ex_o            (flush_ex_o),
      .flush_bp_o            (flush_bp_o),
      .flush_icache_o        (flush_icache_o),
      .flush_dcache_o        (flush_dcache_o),
      .flush_dcache_ack_i    (flush_dcache_ack_i),
      .flush_tlb_o           (flush_tlb_o),
      .flush_tlb_vvma_o      (flush_tlb_vvma_o),
      .flush_tlb_gvma_o      (flush_tlb_gvma_o),
      .halt_csr_i            (halt_csr_i),
      .halt_acc_i            (halt_acc_i),
      .halt_frontend_o       (halt_frontend_o),
      .halt_o                (halt_o),
      .eret_i                (eret_i),
      .ex_valid_i            (ex_valid_i),
      .set_debug_pc_i        (set_debug_pc_i),
      .resolved_branch_i     (resolved_branch),
      .flush_csr_i           (flush_csr_i),
      .fence_i_i             (fence_i_i),
      .fence_i               (fence_i),
      .sfence_vma_i          (sfence_vma_i),
      .hfence_vvma_i         (hfence_vvma_i),
      .hfence_gvma_i         (hfence_gvma_i),
      .flush_commit_i        (flush_commit_i),
      .flush_acc_i           (flush_acc_i)
  );

endmodule
