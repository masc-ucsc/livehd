// PACKED-ARRAY-OF-WIRES dependency tree: a 16-to-4 priority encoder built as
// four generate levels over `wire [7:0] x [3:0]`.
//
// Level l reads level l-1 and writes DISJOINT BIT RANGES of its own entry, so
// every element of `flags`/`codes` is written by several separate continuous
// assignments and read by the next level. A reader that tracks dependencies at
// WORD granularity -- one net per array entry rather than per bit range -- sees
// level l depending on itself and reports a false combinational loop, which is
// a hard compile error rather than a wrong answer.
//
// `codes[l]` also packs a growing (l+1)-bit field per lane, so the lane offsets
// differ at every level and a reader that assumes a fixed stride mispacks them.
module packed_wire_tree(input [15:0] a, output valid, output [3:0] encoded);
  wire [7:0] flags [3:0];
  wire [7:0] codes [3:0];
  for (genvar n = 0; n < 8; n = n + 1) begin : leaf
    assign flags[0][n] = |a[2*n+:2];
    assign codes[0][n] = a[2*n+1];
  end
  for (genvar l = 1; l < 4; l = l + 1) begin : level
    for (genvar n = 0; n < (8 >> l); n = n + 1) begin : lane
      assign flags[l][n] = |flags[l-1][2*n+:2];
      assign codes[l][n*(l+1)+:l+1] = flags[l-1][2*n+1]
        ? {1'b1, codes[l-1][(2*n+1)*l+:l]}
        : {1'b0, codes[l-1][2*n*l+:l]};
    end
  end
  assign valid   = flags[3][0];
  assign encoded = codes[3][3:0];
endmodule
