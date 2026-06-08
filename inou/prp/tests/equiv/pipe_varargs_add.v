// Golden for pipe_varargs_add.prp — `add_all` specializes into a one-stage pipe
// summing its three var-args; `top` lands the registered result at cycle 1.
module \pipe_varargs_add.top (
  input            clock,
  input      [7:0] a,
  input      [7:0] b,
  input      [7:0] c,
  output reg [9:0] z
);
  always @(posedge clock) begin
    z <= a + b + c;
  end
endmodule
