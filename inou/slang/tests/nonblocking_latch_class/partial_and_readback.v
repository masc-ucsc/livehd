// Classification-only fixture (the latch contract refuses the self-updating
// latches it declares, so it never goes past the reader): a level-sensitive
// nonblocking block that writes only part of a variable, or reads back what it
// writes with `<=`, keeps the M1 latch classification.
module partial_and_readback (
    input        c,
    input  [3:0] d,
    input  [3:0] e,
    output reg [3:0] p,
    output reg [3:0] s
);
  // A partial write leaves the other bits holding their value.
  always @(*) begin
    p[1:0] <= e[1:0];
  end
  // Reading back a nonblocking target sees its old value, not the settled one.
  always @(*) begin
    s <= d;
    if (s[0]) s <= e;
  end
endmodule
