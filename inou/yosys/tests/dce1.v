module dce1( input a, input b, output c);
always @(a or b) begin
  c = a | b;

  c = a ^ b;
end
endmodule

