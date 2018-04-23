module trivial3( input a, output c, output d);
assign c = ~a;
assign d = a;
endmodule

module trivial2( input a, input b, output c);
wire tmp1;
assign tmp1 = ~a ^ b;

wire t3o1;
wire t3o2;
trivial3 t3 (tmp1, t3o1, t3o2);

wire tmp2;
assign tmp2 = t3o1 & t3o2;
assign c = !tmp2;
endmodule

