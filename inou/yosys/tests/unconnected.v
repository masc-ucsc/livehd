

module unconnected(input a, input b, output c);
inn foo(.a(a), .b(b), .c(c), .d(), .e());
endmodule

module inn(input a, input b, input d, output c, output e);

assign c = a & b;
assign e = a & d;
endmodule

