// :test: verilog
// The alu/md pair is a false hierarchy loop, so pass.legalize inlines alu into
// the top. The orphaned alu def must stay a submodule for pass.formal: its
// `unique case` overlaps only over free inputs, never under the top's s / ~s.
// Not the lec tier: the re-emitted Verilog still carries alu as an
// uninstantiated module, which is a root there.
module alu (input logic a_sel, input logic b_sel, input logic [3:0] opa, input logic [3:0] opb,
            output logic [3:0] res);
  always_comb begin
    unique case (1'b1)
      a_sel:   res = opa;
      b_sel:   res = opb;
      default: res = 4'd0;
    endcase
  end
endmodule

module md (input logic c, input logic [3:0] res_in, output logic [3:0] opa_out);
  assign opa_out = c ? res_in : 4'd0;
endmodule

module onehot_inlined_submodule (input logic s, input logic [3:0] b, output logic [3:0] o);
  logic [3:0] res, opa;
  alu u_alu (.a_sel(s), .b_sel(~s), .opa(opa), .opb(b), .res(res));
  md  u_md  (.c(1'b0), .res_in(res), .opa_out(opa));
  assign o = res;
endmodule
