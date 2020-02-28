module buff
(
  input  A,
  input  B,

  output C,
  output D
);

always @(*) begin
  C = A;
  D = B;
end

endmodule
