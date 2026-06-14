module cva6_tlb_gate
  import ariane_pkg::*;
#(
  parameter config_pkg::cva6_cfg_t CVA6Cfg = build_config_pkg::build_config(
    cva6_config_pkg::cva6_cfg
  ),
  parameter int unsigned HYP_EXT = 0
)
(
  input  logic clk_i,
  input  logic rst_ni,
  input  logic flush_i,
  input  logic flush_vvma_i,
  input  logic flush_gvma_i,
  input  logic s_st_enbl_i,
  input  logic g_st_enbl_i,
  input  logic v_i,
  input  logic update_valid_i,
  input  logic lu_access_i,
  input  logic [CVA6Cfg.ASID_WIDTH-1:0] lu_asid_i,
  input  logic [CVA6Cfg.VMID_WIDTH-1:0] lu_vmid_i,
  input  logic [CVA6Cfg.VLEN-1:0] lu_vaddr_i,
  output logic [CVA6Cfg.GPLEN-1:0] lu_gpaddr_o,
  output logic [63:0] lu_content_bits_o,
  output logic [63:0] lu_g_content_bits_o,
  input  logic [CVA6Cfg.ASID_WIDTH-1:0] asid_to_be_flushed_i,
  input  logic [CVA6Cfg.VMID_WIDTH-1:0] vmid_to_be_flushed_i,
  input  logic [CVA6Cfg.VLEN-1:0] vaddr_to_be_flushed_i,
  input  logic [CVA6Cfg.GPLEN-1:0] gpaddr_to_be_flushed_i,
  output logic [CVA6Cfg.PtLevels-2:0] lu_is_page_o,
  output logic lu_hit_o
);
  typedef struct packed {
    logic n;
    logic [8:0] reserved;
    logic [CVA6Cfg.PPNW-1:0] ppn;
    logic [1:0] rsw;
    logic d;
    logic a;
    logic g;
    logic u;
    logic x;
    logic w;
    logic r;
    logic v;
  } pte_cva6_t;

  typedef struct packed {
    logic valid;
    logic is_napot_64k;
    logic [CVA6Cfg.PtLevels-2:0][HYP_EXT:0] is_page;
    logic [CVA6Cfg.VpnLen-1:0] vpn;
    logic [CVA6Cfg.ASID_WIDTH-1:0] asid;
    logic [CVA6Cfg.VMID_WIDTH-1:0] vmid;
    logic [HYP_EXT*2:0] v_st_enbl;
    pte_cva6_t content;
    pte_cva6_t g_content;
  } tlb_update_cva6_t;

  tlb_update_cva6_t update_i;
  pte_cva6_t lu_content;
  pte_cva6_t lu_g_content;

  assign update_i = '{
    valid: update_valid_i,
    is_napot_64k: 1'b0,
    is_page: '0,
    vpn: '0,
    asid: '0,
    vmid: '0,
    v_st_enbl: '1,
    content: '0,
    g_content: '0
  };

  assign lu_content_bits_o = lu_content;
  assign lu_g_content_bits_o = lu_g_content;

  cva6_tlb #(
    .CVA6Cfg(CVA6Cfg),
    .pte_cva6_t(pte_cva6_t),
    .tlb_update_cva6_t(tlb_update_cva6_t),
    .TLB_ENTRIES(4),
    .HYP_EXT(HYP_EXT)
  ) dut (
    .clk_i(clk_i),
    .rst_ni(rst_ni),
    .flush_i(flush_i),
    .flush_vvma_i(flush_vvma_i),
    .flush_gvma_i(flush_gvma_i),
    .s_st_enbl_i(s_st_enbl_i),
    .g_st_enbl_i(g_st_enbl_i),
    .v_i(v_i),
    .update_i(update_i),
    .lu_access_i(lu_access_i),
    .lu_asid_i(lu_asid_i),
    .lu_vmid_i(lu_vmid_i),
    .lu_vaddr_i(lu_vaddr_i),
    .lu_gpaddr_o(lu_gpaddr_o),
    .lu_content_o(lu_content),
    .lu_g_content_o(lu_g_content),
    .asid_to_be_flushed_i(asid_to_be_flushed_i),
    .vmid_to_be_flushed_i(vmid_to_be_flushed_i),
    .vaddr_to_be_flushed_i(vaddr_to_be_flushed_i),
    .gpaddr_to_be_flushed_i(gpaddr_to_be_flushed_i),
    .lu_is_page_o(lu_is_page_o),
    .lu_hit_o(lu_hit_o)
  );
endmodule
