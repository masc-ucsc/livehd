// Golden for spec_memory_bulk_read_visibility: the clear is the LAST write of the
// cycle, so it wins over the entry write in every memory.
module spec_memory_bulk_read_visibility(input clk, input clr, input we, input [1:0] a, input [7:0] d,
                                        input [1:0] ra, output [7:0] pb, output [7:0] pa,
                                        output [7:0] fb, output [7:0] oa);
  reg [7:0] p [0:3];
  reg [7:0] f [0:3];
  reg [7:0] o [0:3];
  integer k;
  always @(posedge clk) begin
    if (we) begin p[a] <= d; f[a] <= d; o[a] <= d; end
    if (clr) for (k = 0; k < 4; k = k + 1) begin p[k] <= 8'h0; f[k] <= 8'h0; o[k] <= 8'h0; end
  end
  assign pb = p[ra];
  assign pa = clr ? 8'h0 : ((we && a == ra) ? d : p[ra]);
  assign fb = clr ? 8'h0 : ((we && a == ra) ? d : f[ra]);
  assign oa = o[ra];
endmodule
