/*
Transparent hierarchy wrappers (graph/README.md): an instance named
`__flat___<suffix>` adds no level to the logical name, so the variants' leaf
register is the flat golden's `foo.q`.
:set: compile.upass.inline=false
:lec_set: formal.engine=ind formal.simfail=false pass.satopt=false
*/
module top(input clk, rst, d, output q);
reg \foo.q ;
always @(posedge clk) if (rst) \foo.q <= 0; else \foo.q <= d;
assign q = \foo.q ;
endmodule
