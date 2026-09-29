// A PARTIALLY-REGISTERED variable: some bits driven by a continuous `assign`,
// the rest nonblocking-written by an edge process. IEEE 1800 allows it as long
// as the two drivers do not overlap, and it is how bedrock-rtl's br_delay and
// cvfpu's pipelines spell a delay line whose stage 0 IS the input:
//
//     assign stages[0] = in;
//     for (genvar i = 1; i <= N; i++) always_ff stages[i] <= stages[i-1];
//
// Lowering that as ONE register silently registers the continuous bits: every
// one of them gains a cycle of delay and reads 0 out of reset instead of the
// live input, and `out` comes out N+1 cycles late instead of N. br_delay's
// `out_stages` LEC-REFUTED on exactly this (ref=0 impl=254 at step 1).
//
// `delay_flat` is the same shape on a plain vector (bit-slice drivers, no
// packed array) so the fix is pinned on both lvalue paths, and `delay_port`
// puts it on a module OUTPUT. nocheck_ keeps the file out of the plain-yosys
// glob (SV genvar/logic declarations).
module delay_arr (
    input  logic        clk,
    input  logic        rst,
    input  logic [7:0]  d,
    output logic [7:0]  q,
    output logic [31:0] all
);
  logic [3:0][7:0] stages;

  assign stages[0] = d;

  for (genvar i = 1; i <= 3; i++) begin : gen_stages
    always_ff @(posedge clk) begin
      if (rst) stages[i] <= 8'h0;
      else stages[i] <= stages[i-1];
    end
  end

  assign q   = stages[3];
  assign all = stages;
endmodule

module delay_flat (
    input  logic        clk,
    input  logic        rst,
    input  logic [7:0]  d,
    output logic [31:0] all
);
  logic [31:0] stages;
  assign stages[7:0] = d;
  always_ff @(posedge clk) begin
    if (rst) stages[31:8] <= 24'h0;
    else stages[31:8] <= stages[23:0];
  end
  assign all = stages;
endmodule

// The same split ON A MODULE OUTPUT PORT: the port itself becomes the
// composite (it needs the seed, never a second declare).
module delay_port (
    input  logic        clk,
    input  logic        rst,
    input  logic [7:0]  d,
    output logic [31:0] stages
);
  assign stages[7:0] = d;
  always_ff @(posedge clk) begin
    if (rst) stages[31:8] <= 24'h0;
    else stages[31:8] <= stages[23:0];
  end
endmodule

module slang_partial_reg (
    input  logic        clk,
    input  logic        rst,
    input  logic [7:0]  d,
    output logic [7:0]  q_arr,
    output logic [31:0] all_arr,
    output logic [31:0] all_flat,
    output logic [31:0] all_port
);
  delay_arr  u_arr (.clk(clk), .rst(rst), .d(d), .q(q_arr), .all(all_arr));
  delay_flat u_flat(.clk(clk), .rst(rst), .d(d), .all(all_flat));
  delay_port u_port(.clk(clk), .rst(rst), .d(d), .stages(all_port));
endmodule
