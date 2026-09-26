/*
The mapped shape: the select stage split into one-bit cells under other
names (no counterpart on the reference), the array as separate registers,
the sync reset folded into the next-state.
*/
module top(input clk, input rst, input addr_valid, input [1:0] sel_in,
           input rd_valid, input [3:0] rd_data, input [1:0] osel, output [3:0] head);
  reg sv, xs0, xs1;
  reg [3:0] h0, h1;
  always @(posedge clk) begin
    sv  <= !rst & addr_valid;
    xs0 <= sv ? sel_in[0] : xs0;
    xs1 <= sv ? sel_in[1] : xs1;
    h0  <= rst ? 4'd0 : (rd_valid & xs0) ? rd_data : h0;
    h1  <= rst ? 4'd0 : (rd_valid & xs1) ? rd_data : h1;
  end
  assign head = ({4{osel[0]}} & h0) | ({4{osel[1]}} & h1);
endmodule
