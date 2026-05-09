module reg_enable(
  input        clk,
  input        reset,
  input        en,
  input  [3:0] din,
  output [3:0] q
);
reg [3:0] r;

always @(posedge clk) begin
  if (reset) begin
    r <= 4'd0;
  end else if (en) begin
    r <= din;
  end
end

assign q = r;
endmodule
