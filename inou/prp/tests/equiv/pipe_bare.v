module \pipe_bare.bare (
  input            clock,
  input      [7:0] a,
  output reg [7:0] c
);

  always @(posedge clock) begin
    c <= a;
  end

endmodule
