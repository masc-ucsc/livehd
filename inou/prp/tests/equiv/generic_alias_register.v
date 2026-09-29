module generic_alias_register(input clock, reset,
  input signed [9:0] a, b, output signed [9:0] s);
  add__s10_s10 node(.clock(clock), .reset(reset), .a(a), .b(b), .s(s));
endmodule

module add__s10_s10(input clock, reset,
  input signed [9:0] a, b, output signed [9:0] s);
  reg signed [9:0] s_r;
  always @(posedge clock) if (reset) s_r <= 0; else s_r <= a + b;
  assign s = s_r;
endmodule
