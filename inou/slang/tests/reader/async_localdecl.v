// async-reset always_ff with a named block + a block-local temp before the
// if/else reset rung (the dcache/harness idiom). The native reader must hoist
// the local declaration into the clocked prologue and still extract the reset.
module async_localdecl
  ( input            clk
  , input            rst_ni
  , input      [7:0] d
  , output reg [7:0] q
  );
  always_ff @(posedge clk or negedge rst_ni) begin : p
    logic [7:0] tmp;
    if (!rst_ni) begin
      q <= 8'h0;
    end else begin
      tmp = d ^ 8'haa;
      q   <= tmp;
    end
  end
endmodule
