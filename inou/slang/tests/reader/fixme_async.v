
// Moved to FIXME because yosys can not check equivalence with itself (maybe
// future?) or new checker?

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


