module \mod_call_pipe2.mul (
  input             clock,
  input      [15:0] a,
  input      [15:0] b,
  output reg [31:0] c
);

  always @(posedge clock) begin
    c <= a * b;
  end

endmodule

module \mod_call_pipe2.top (
  input             clock,
  input      [15:0] in1,
  input      [15:0] in2,
  output reg [31:0] out
);

  wire [31:0] c;
  reg  [31:0] p0;

  \mod_call_pipe2.mul u_mul (.clock(clock), .a(in1), .b(in2), .c(c));

  always @(posedge clock) begin
    p0  <= c;
    out <= p0;
  end

endmodule
