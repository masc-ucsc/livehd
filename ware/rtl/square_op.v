module square_op
(
  input   Ai,
  input   Bi,

  output  Pi,
  output  Gi,
  output  Hi
);

assign Pi = Ai ^ Bi;
assign Gi = Ai & Bi;
assign Hi = Ai ^ Bi;

endmodule
