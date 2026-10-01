// Golden for mem_pair_occurrence_order (see the .prp): `mf` has no reset, `mr`
// resets to 0; both are [4]x16, declared in the order the compiled Pyrope side
// does NOT list them in, so only a pairing by name matches them up.
module mem_pair_occurrence_order (
  input         clock,
  input         reset,
  input         wf,
  input         wb,
  input  [1:0]  a,
  input  [15:0] d,
  input  [1:0]  ra,
  output [15:0] qf,
  output [15:0] qr
);
  reg [15:0] mf[3:0];
  reg [15:0] mr[3:0];
  integer    i;

  always @(posedge clock) begin
    if (wf) mf[a] <= d;
    if (reset) begin
      for (i = 0; i < 4; i = i + 1) mr[i] <= 16'd0;
    end else if (wb) begin
      mr[a] <= d;
    end
  end

  assign qf = mf[ra];
  assign qr = mr[ra];
endmodule
