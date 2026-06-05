module \pipe1_pass.passthru (
  input            clock,
  input      [7:0] a,
  output reg [7:0] o
);

  always @(posedge clock) begin
    o <= a;
  end

endmodule
