module full_adder
(
  input   a,
  input   b,
  input   cin,

  output  reg cout,
  output  reg sum
);

always @(*) begin
 cout = ((a^b)&cin)|(a&b);
 sum = (a^b^cin);
end

endmodule
