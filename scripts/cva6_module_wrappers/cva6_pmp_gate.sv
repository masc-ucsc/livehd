// One-line instantiation top for CVA6's `pmp` (physical memory protection
// permission check).  `CVA6Cfg` is the only parameter; the `riscv::` enum/struct
// ports are exposed as plain vectors here and cast at the instantiation, so the
// top has no package types in its signature.
//
// Cone pulls in NrPMPEntries x `pmp_entry` plus `lzc`, all combinational and all
// already in the filelist.  Address/permission logic: mid-width and boolean-heavy,
// a useful middle ground between the decode blocks and the wide datapaths.
module cva6_pmp_gate
  import ariane_pkg::*;
#(
    parameter config_pkg::cva6_cfg_t CVA6Cfg = build_config_pkg::build_config(
        cva6_config_pkg::cva6_cfg
    )
) (
    input  logic [                              CVA6Cfg.PLEN-1:0] addr_i,
    input  logic [               $bits(riscv::pmp_access_t)-1:0]  access_type_i,
    input  logic [                 $bits(riscv::priv_lvl_t)-1:0]  priv_lvl_i,
    input  logic [avoid_neg(CVA6Cfg.NrPMPEntries-1):0][CVA6Cfg.PLEN-3:0] conf_addr_i,
    input  logic [avoid_neg(CVA6Cfg.NrPMPEntries-1):0][$bits(riscv::pmpcfg_t)-1:0] conf_i,
    output logic                                                   allow_o
);

  riscv::pmpcfg_t [avoid_neg(CVA6Cfg.NrPMPEntries-1):0] conf;

  always_comb begin
    for (int unsigned k = 0; k <= avoid_neg(CVA6Cfg.NrPMPEntries - 1); k++) begin
      conf[k] = riscv::pmpcfg_t'(conf_i[k]);
    end
  end

  pmp #(
      .CVA6Cfg(CVA6Cfg)
  ) dut (
      .addr_i       (addr_i),
      .access_type_i(riscv::pmp_access_t'(access_type_i)),
      .priv_lvl_i   (riscv::priv_lvl_t'(priv_lvl_i)),
      .conf_addr_i  (conf_addr_i),
      .conf_i       (conf),
      .allow_o      (allow_o)
  );

endmodule
