// A flop with TWO asynchronous rungs (async SET and async RESET) has no
// lowering: LiveHD's flop has ONE reset_pin/initial slot, and Pyrope spells one
// reset (`reset_pin`, `async`). The reader refuses it by name (`dual-async-reset`)
// instead of letting upass.attributes report a generic "attribute is already set"
// conflict. (Formerly the fixme pair prp-{equiv,statematch,v2prp2v}-dual_async_set_reset.)
// :test: error
// :error: two asynchronous reset/set rungs
module dual_async_set_reset_reject(
  input  clk,
  input  D,
  input  S,
  input  R,
  output reg Q
);
  always @(posedge clk, negedge S, negedge R) begin
    if (!S)
      Q <= 1'b1;
    else if (!R)
      Q <= 1'b0;
    else
      Q <= D;
  end
endmodule
