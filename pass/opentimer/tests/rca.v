module rca (
inp1,
inp2,
tau2015_clk,
out
);

// Start PIs
input inp1;
input inp2;
input tau2015_clk;

// Start POs
output out;

// Start wires
wire inp1;
wire inp2;
wire tau2015_clk;
wire out;
wire n0;

FADDER FA0 (
.A(inp1),
.B(inp2),
.C(carry0),
.YS( ),
.YC(n0)
);

FADDER FAN (
.A(),
.B(),
.C(n0),
.YS(),
.YC(out)
);

endmodule
