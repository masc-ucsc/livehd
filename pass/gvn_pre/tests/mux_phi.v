module mux_vs_phi (
  input sel, a, b, c,
  output d
);

always @ (*) begin
  if (sel)
    d = a & b;
  else
    d = a & b | c;
end

endmodule

module mux_pre1 (
  input sel, a, b,
  output z
);

reg c, d;

always @ (*) begin
  d = 1'b1;
  if (sel)
    d = a & b;
    c = a;
  else
    c = b;
  z = a & b;
end

endmodule
