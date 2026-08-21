// A generate-block local that SHADOWS a module-scope name of the same
// identifier (legal SystemVerilog: an instantiated generate block is its own
// scope), where BOTH copies are written from an unrolled `for` body whose `if`
// guard is compile-time decidable.
//
// Reduced from cvfpu's fpnew_opgroup_multifmt_slice (`local_operands`, written
// under `if (i == 2) … else …`). The slang reader declares a plain packed local
// LAZILY at its first lowered touch, so the declaration landed inside the THEN
// arm -- which upass then deleted as dead code, taking the declaration with it.
// The emitted Pyrope kept the writes (they live in the surviving sibling arm)
// but declared nothing, and no longer recompiled:
//
//   error: assignment to undeclared variable 'gen_lanes_0_active_lane_lanes'
//
// The shadowing also pins the naming half: the two `lanes` may not collapse
// onto one Pyrope net, so the generate-block copy must keep its flattened
// `gen_lanes_0_active_lane_` path.
module local_shadow_comptime_arm (
    input  wire [23:0] operands_i,
    input  wire [2:0]  sh_i,
    output wire [23:0] outer_o,
    output wire [23:0] inner_o
);

  // module scope
  reg [23:0] lanes;
  always @(*) begin
    for (integer i = 0; i < 3; i = i + 1) begin
      if (i == 2) begin
        lanes[i*8+:8] = operands_i[i*8+:8] << sh_i;
      end else begin
        lanes[i*8+:8] = operands_i[i*8+:8] >> sh_i;
      end
    end
  end
  assign outer_o = lanes;

  // generate-block scope: `lanes` here SHADOWS the module-scope `lanes` above
  genvar lane;
  generate
    for (lane = 0; lane < 1; lane = lane + 1) begin : gen_lanes
      if (lane == 0) begin : active_lane
        reg [23:0] lanes;
        always @(*) begin
          for (integer i = 0; i < 3; i = i + 1) begin
            if (i == 2) begin
              lanes[i*8+:8] = operands_i[i*8+:8] >> sh_i;
            end else begin
              lanes[i*8+:8] = operands_i[i*8+:8] << sh_i;
            end
          end
        end
        assign inner_o = lanes;
      end
    end
  endgenerate

endmodule
