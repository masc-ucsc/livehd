
module async(y,clk,reset);

input clk, reset;
output reg y;

always @(posedge clk, posedge reset) begin
if (reset)
y <= 0;
else
y <= y + 1;
end
endmodule


