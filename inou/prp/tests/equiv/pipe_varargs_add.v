// Golden for pipe_varargs_add.prp — `add_all` specializes into a one-stage pipe
// summing its three var-args; `top` lands the registered result at cycle 1.
module \pipe_varargs_add.add_all__u8_u8_u8 (
  input            clock,
  input      [7:0] args__0,
  input      [7:0] args__1,
  input      [7:0] args__2,
  output reg [9:0] r
);
  always @(posedge clock) begin
    r <= args__0 + args__1 + args__2;
  end
endmodule

module \pipe_varargs_add.top (
  input            clock,
  input      [7:0] a,
  input      [7:0] b,
  input      [7:0] c,
  output     [9:0] z
);
  \pipe_varargs_add.add_all__u8_u8_u8 u_add_all (
    .clock(clock), .args__0(a), .args__1(b), .args__2(c), .r(z)
  );
endmodule
