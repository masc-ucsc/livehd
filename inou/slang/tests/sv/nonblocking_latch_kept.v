// :test: lec
// An incomplete level-sensitive nonblocking block still holds its value: it
// stays the M1 latch idiom even though a sibling block of the same style (r) is
// combinational.
module nonblocking_latch_kept (
    input        en,
    input  [3:0] d,
    input  [3:0] e,
    output reg [3:0] q,
    output reg [3:0] r
);
  always @(*) begin
    if (en) q <= d;
  end
  always @(*) begin
    r <= 4'd0;
    if (en) r <= e;
  end
endmodule
