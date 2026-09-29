// :test: roundtrip
// :top: d\e
// Preserve escaped entity names, nested array initialization, and zero-extended
// concatenation through writer emission, recompilation, and equivalence.
module \d\e (
  input        sel,
  input        en,
  input  [3:0] d,
  output reg [3:0] out,
  output reg [7:0] o2
);
  reg [3:0] mem [1:0];
  initial begin
    mem[0] <= 4'd1;
    mem[1] <= 4'd2;
  end
  always @(*) begin
    out = 4'd0;
    o2  = 8'd0;
    if (en) begin
      out = mem[sel];
      o2  = {4'd0, d} + 8'd7;
    end
  end
endmodule
