// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// A plain packed local whose FIRST lowered write sits inside a comptime-decided
// `if` arm. Reduced from cvfpu's fpnew_opgroup_multifmt_slice (`local_operands`
// written from an unrolled `for` body's `if (i == 2) … else …`). The declare is
// emitted lazily at first touch, so without a module-top pre-declare it lands
// in the THEN arm -- which upass then deletes as dead, taking the declaration
// with it. The emitted Pyrope no longer declares the net at all and fails to
// recompile ("assignment to undeclared variable").
module comptime_arm_declare_hoist (
    input  logic [2:0][31:0] operands_i,
    input  logic [4:0]       sh_i,
    output logic [2:0][31:0] flat_o,
    output logic [2:0][31:0] gen_o
);

  // (1) module scope, no generate: the arm guard is a literal constant.
  logic [2:0][31:0] flat_operands;
  always_comb begin
    for (int unsigned i = 0; i < 3; i++) begin
      if (i == 2) begin
        flat_operands[i] = operands_i[i] << sh_i;
      end else begin
        flat_operands[i] = operands_i[i] >> sh_i;
      end
    end
  end
  assign flat_o = flat_operands;

  // (2) the cvfpu shape: the same local declared inside a generate-if block,
  // so its emitted name carries the flattened `gen_lanes_0_active_lane_` path.
  for (genvar lane = 0; lane < 1; lane++) begin : gen_lanes
    if (lane == 0) begin : active_lane
      logic [2:0][31:0] local_operands;
      always_comb begin : prepare_input
        for (int unsigned i = 0; i < 3; i++) begin
          if (i == 2) begin
            local_operands[i] = operands_i[i] << sh_i;
          end else begin
            local_operands[i] = operands_i[i] >> sh_i;
          end
        end
      end
      assign gen_o = local_operands;
    end
  end

endmodule
