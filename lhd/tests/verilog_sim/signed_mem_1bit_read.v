// A signed 1-bit memory element widened on read: 1'sb1 is -1, so q is 8'hff.
module signed_mem_1bit_read(input clock, input reset, input signed [11:0] d, input signed [1:0] s,
                            input [1:0] wa, input [1:0] ra, output o, output [7:0] q);
  reg signed mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else mem[wa] <= (d >> s);
  end
  wire signed mrd = mem[ra];
  assign o = mrd;
  assign q = mrd;
endmodule
