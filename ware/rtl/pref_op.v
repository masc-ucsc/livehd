module pref_op
(
  input   gi,
  input   pi,
  
  input   gk,
  input   pk,

  output  go,
  output  po
);

always @(*) begin
  go = gi | (pi & gk);  // g0 = gi + pi.gk
  po = pi & pk;         // p0 = pi.pk
end

endmodule

