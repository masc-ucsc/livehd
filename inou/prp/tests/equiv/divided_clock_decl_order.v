// Regression: prp_writer declaring a register BEFORE the register that clocks it.
//
// `q` is clocked by `dclk`, another register in the same module (a divided
// clock). The writer emitted the declares in LNAST order, so
// `reg q:[clock_pin=ref dclk]` landed ABOVE `reg dclk` and the re-read failed
// with "read of undefined variable 'dclk'". The pin machinery makes such a net
// position-independent for tolg (a flop Q is order-free state), but Pyrope's
// scope check is textual: the declaration still has to come first.
module divided_clock_decl_order(input clk, input d, output reg q);
  reg [1:0] cnt;
  reg dclk;
  always @(posedge clk) begin
    cnt <= cnt + 2'b1;
    dclk <= cnt[1];
  end
  always @(posedge dclk) q <= d;
endmodule
