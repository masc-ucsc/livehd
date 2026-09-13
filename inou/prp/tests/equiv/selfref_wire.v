module selfref_wire (
  input  [3:0] a,
  output [3:0] y
);

  // A word-level combinational "loop" that is ACYCLIC bit by bit:
  //   t[0] = a[0], t[1] = t[0], t[2] = t[1], t[3] = t[2]
  // so t settles at {4{a[0]}}. Legal Verilog; firtool emits this shape for
  // every bit-sliced AES/SM4 S-box in xiangshan.
  wire [3:0] t;
  assign t = {t[2], t[1], t[0], a[0]};

  assign y = t;

endmodule
