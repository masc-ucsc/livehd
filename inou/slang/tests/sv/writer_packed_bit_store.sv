// :test: roundtrip
// :lec_timeout: 30
module writer_packed_bit_store(input clk, reset, en, d, input row, input [1:0] bitidx, output o);
logic [1:0][3:0] bank;
always @(posedge clk) begin
 if (reset) bank <= 0;
 else if (en) bank[0][1] <= d;
end
assign o = bank[row][bitidx];
endmodule
