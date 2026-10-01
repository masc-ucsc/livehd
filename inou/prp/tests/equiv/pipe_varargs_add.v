// Golden for pipe_varargs_add.prp — `add_all` specializes into a one-stage pipe
// summing its three named var-args (fields a, b, c of `args`, ports args__a..c);
// `top` lands the registered result at cycle 1.
module \pipe_varargs_add.add_all__U8_U8_U8 (
  input            clock,
  input      [7:0] args__a,
  input      [7:0] args__b,
  input      [7:0] args__c,
  output reg [9:0] r
);
  always @(posedge clock) begin
    r <= args__a + args__b + args__c;
  end
endmodule

module \pipe_varargs_add.top (
  input            clock,
  input      [7:0] a,
  input      [7:0] b,
  input      [7:0] c,
  output     [9:0] z
);
  \pipe_varargs_add.add_all__U8_U8_U8 u_add_all (
    .clock(clock), .args__a(a), .args__b(b), .args__c(c), .r(z)
  );
endmodule
