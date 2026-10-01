// Golden for mem_replay_whole_copy.prp: Verilog nonblocking writes, with the
// Pyrope program-order reads spelled out.
module mem_replay_whole_copy(input clock, input w, input cp, input [1:0] a, input [5:0] d, input [1:0] rb,
                             output [5:0] r, output [5:0] ro);
  reg [5:0] m[0:3];
  reg [5:0] o[0:3];
  reg [5:0] n[0:3];
  reg [5:0] k[0:3];
  integer i;
  always @(posedge clock) begin
    if (w) begin
      m[a] <= d;
      o[a] <= d;
    end
    if (cp) for (i = 0; i < 4; i = i + 1) begin
      n[i] <= (w && a == i) ? d : m[i];
      k[i] <= o[i];
    end
  end
  wire [5:0] mr = (w && a == rb) ? d : m[rb];
  assign r  = cp ? mr : n[rb];
  assign ro = cp ? o[rb] : k[rb];
endmodule
