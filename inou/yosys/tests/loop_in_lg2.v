module loop_in_lg2(
  input clk,
  input [7:0] a,
  input [7:0] b,
  output reg [1:0] x
);

  reg [7:0] regi;

  always @(posedge clk) begin
    if ((regi - 1) > 0) begin
      regi <= a;
      x <= 'h2;
    end else begin
      regi <= b;
      x <= 'h1;
    end
  end
endmodule
