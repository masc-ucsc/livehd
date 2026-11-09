module memory_array(input clock, input reset, input [1:0] wa, input [7:0] wd, input we, input [1:0] ra, output [7:0] rd);
  reg [3:0][7:0] tab;
  wire [3:0][7:0] nxt;
  genvar i;
  generate for (i = 0; i < 4; i = i + 1) begin : g
    assign nxt[i] = (we && wa == i) ? wd : tab[i];
  end endgenerate
  always @(posedge clock or posedge reset) begin
    if (reset) tab <= 32'h0;
    else tab <= nxt;
  end
  assign rd = tab[ra];
endmodule
