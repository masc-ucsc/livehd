module inner (
  input            clock,
  input            reset,
  input      [7:0] a,
  output     [7:0] out
);
  reg [7:0] q;
  always @(posedge clock) begin
    if (reset)
      q <= 8'h00;
    else
      q <= q ^ a;
  end
  assign out = q;
endmodule

module \conditional_state_call.top (
  input            clock,
  input            reset,
  input            en,
  input      [7:0] a,
  output     [7:0] out
);
  reg gated_en;
  always_latch begin
    if (!clock)
      gated_en <= en | reset;
  end
  wire gated_clock = clock & gated_en;
  wire [7:0] inner_out;

  inner u_inner_out_0 (
    .clock(gated_clock),
    .reset(reset),
    .a(a),
    .out(inner_out)
  );

  assign out = en ? inner_out : 8'h00;
endmodule
