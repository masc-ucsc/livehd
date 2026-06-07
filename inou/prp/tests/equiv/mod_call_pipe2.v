module \mod_call_pipe2.top (
  input             clock,
  input      [15:0] in1,
  input      [15:0] in2,
  output reg [31:0] out
);

  reg [31:0] p0, p1;

  always @(posedge clock) begin
    p0  <= in1 * in2;
    p1  <= p0;
    out <= p1;
  end

endmodule
