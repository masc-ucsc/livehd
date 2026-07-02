/* verilator lint_off WIDTH */
// Golden for flop_init_headroom: the async-reset CONTROL flop `vld` uses
// cgen's signed spare-bit emission convention (`reg [1:0]` holding a u1
// value: writes only ever store {0,1}; value reads mask with `& 1`, but the
// control read `vld != 0` consumes the reg unmasked). The Pyrope side
// declares the same flop 1 bit wide. Equivalent from any value-consistent
// power-on state; a LEC that leaves the wide reg's pre-reset headroom bit
// free (shared init symbol at MAX width) refutes spuriously — see the .prp
// header.
module \flop_init_headroom.top (
   input signed clock
  ,input signed reset
  ,input signed io_en
  ,input signed io_d
  ,output reg signed io_q
);
reg [1:0] vld;
reg [1:0] ___next_vld;
reg dat;
reg ___next_dat;
reg en_q;
always_comb begin
  ___next_vld = io_en & 1'h1;
  en_q = (vld == ('sb0)) == ('sb0);
  ___next_dat = io_d & 1'h1;
  io_q = (dat & 1'h1);
end
always @(posedge clock or posedge reset ) begin
if (reset) begin
vld <= ('sb0);
end else begin
vld <= ___next_vld;
end
end
always @(posedge clock ) begin
if (en_q) begin
dat <= ___next_dat;
end
end
endmodule
