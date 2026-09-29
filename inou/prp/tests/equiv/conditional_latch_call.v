module inner (
  input            gate,
  input      [7:0] d,
  output     [7:0] q
);
  reg [7:0] l;
  always_latch begin
    if (gate)
      l <= d;
  end
  assign q = l;
endmodule

module \conditional_latch_call.top (
  input            active,
  input            gate,
  input      [7:0] d,
  output     [7:0] q
);
  wire [7:0] inner_q;
  inner u_inner_q_0 (
    .gate(active && gate),
    .d(d),
    .q(inner_q)
  );
  assign q = active ? inner_q : 8'h00;
endmodule
