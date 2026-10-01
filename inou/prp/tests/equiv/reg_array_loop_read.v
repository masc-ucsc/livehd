// Golden for reg_array_loop_read: each array is one packed register; `n*`/`gn*`
// are the next values (the enabled write spliced in), which the program-order
// reads after the write and every fwd read observe. The old reads see the
// committed register.
module reg_array_loop_read (
  input         clock,
  input         reset,
  input         en,
  input  [1:0]  i,
  input  [4:0]  d,
  input  [5:0]  v2,
  input  [19:0] v4,
  output [5:0]  o2,
  output [5:0]  f2,
  output [5:0]  s2,
  output [19:0] o4,
  output [19:0] f4,
  output [19:0] s4
);
  reg  [5:0]  r2, g2, h2;
  reg  [19:0] r4, g4, h4;
  wire        w   = en && !reset;
  wire [5:0]  m2  = 6'h7 << {i[0], 1'b0} + i[0];
  wire [5:0]  n2  = w ? ((r2 & ~m2) | (({3'b0, d[2:0]} << (i[0] * 3)) & m2)) : r2;
  wire [19:0] m4  = 20'h1f << (i * 5);
  wire [19:0] d4  = {15'b0, d} << (i * 5);
  wire [5:0]  gn2 = w ? ((g2 & ~m2) | (({3'b0, d[2:0]} << (i[0] * 3)) & m2)) : g2;
  wire [19:0] n4  = w ? ((r4 & ~m4) | (d4 & m4)) : r4;
  wire [19:0] gn4 = w ? ((g4 & ~m4) | (d4 & m4)) : g4;
  always @(posedge clock) begin
    if (reset) begin
      r2 <= 0; g2 <= 0; h2 <= 0; r4 <= 0; g4 <= 0; h4 <= 0;
    end else begin
      r2 <= n2; g2 <= gn2; r4 <= n4; g4 <= gn4;
      h2 <= w ? ((h2 & ~m2) | (({3'b0, d[2:0]} << (i[0] * 3)) & m2)) : h2;
      h4 <= w ? ((h4 & ~m4) | (d4 & m4)) : h4;
    end
  end
  assign o2 = n2 ^ v2;
  assign f2 = gn2 ^ v2;
  assign s2 = h2 ^ v2;
  assign o4 = n4 ^ v4;
  assign f4 = gn4 ^ v4;
  assign s4 = h4 ^ v4;
endmodule
