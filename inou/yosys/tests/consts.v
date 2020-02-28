
module consts(a,b,c,d,e,f);

input [31:0] a;
output [31:0] b,d;
output [63:0] c,e,f;

assign c = 64'hFFFFFFFF & {a,a};
assign b = 32'hFFFF & a;

assign e = 64'hFFFFFFFFFFFFFFFF & {a,a};
assign d = 32'hFFFFFFFF & a;

assign f = 32'hFFFFFFFF & a;

endmodule

