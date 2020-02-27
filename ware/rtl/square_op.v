module square_op
(
  input   Ai,
  input   Bi,

  output  Pi,
  output  Gi,
  output  Hi
);

always @(*) begin
  Pi = Ai ^ Bi;
  Gi = Ai & Bi;
  Hi = Ai ^ Bi;
end

endmodule

