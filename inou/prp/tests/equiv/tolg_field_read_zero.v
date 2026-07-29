// FIXME tracker -- prp_writer DROPS an array's type and contents.
// The emitted Pyrope below says `mut mem = 0`: both the `[2]u4` array type and the
// `initial` block's element values are gone. tolg then meets the runtime index
// `mem[sel]` on a base that resolved to the scalar constant 0 and aborts with
// "field/index read of '0' could not be resolved". 4 corpus files.
// FIX is in the writer: emit the array declaration (`mut mem:[2]u4 = (1, 2)`).
module tolg_field_read_zero(
input sel,
input en,
output reg [3:0] out
);
reg [3:0] mem [1:0];
initial begin
   mem[0] <= 4'd1;
   mem[1] <= 4'd2;
end
always @(*) begin
  out = 4'd0;
  if (en) out = mem[sel];
end
endmodule
