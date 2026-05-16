module test_and2(clk, in1, in2, out);
  input  clk; // Not used but for easier verilator common flow
  input  in1;
  input  in2;
  output out;

  wire x, y;
  wire z0, z1, z2, z3, z4, z5, z6, z7, z8, z9, z10;
  wire out1, out2, out3, out4, out5;
  wire  in1_buf;
  wire  in2_buf;

  // Compute x = NOT(inp1)
  sky130_fd_sc_hd__inv_1 inv1 (
    .A(in1),
    .Y(in1_buf)
  );

  // Compute y = NOT(inp2)
  sky130_fd_sc_hd__inv_1 inv2 (
    .A(in2),
    .Y(in2_buf)
  );

  sky130_fd_sc_hd__buf_1 gin1 (
    .A(in1_buf),
    .X(x)
  );

  sky130_fd_sc_hd__buf_1 gin2 (
    .A(in2_buf),
    .X(y)
  );

  // z0 = AND(x,y)
  sky130_fd_sc_hd__and2_1 and1 (
    .A(x),
    .B(y),
    .X(z0)
  );

  // z1 = OR(x,y)
  sky130_fd_sc_hd__or2_1 or1 (
    .A(x),
    .B(y),
    .X(z1)
  );

  // z2 = AND(z0,z1)
  sky130_fd_sc_hd__and2_1 and2 (
    .A(z0),
    .B(z1),
    .X(z2)
  );

  // out1 = NOT(z2)
  sky130_fd_sc_hd__inv_1 inv3 (
    .A(z2),
    .Y(out1)
  );

  // z3 = AND(out1, x)
  sky130_fd_sc_hd__and2_1 and3 (
    .A(out1),
    .B(x),
    .X(z3)
  );

  // z4 = OR(z2,y)
  sky130_fd_sc_hd__or2_1 or2 (
    .A(z2),
    .B(y),
    .X(z4)
  );

  // out2 = NOT(z3)
  sky130_fd_sc_hd__inv_1 inv4 (
    .A(z3),
    .Y(out2)
  );

  // z5 = XOR(out1, out2)
  sky130_fd_sc_hd__xor2_1 xor1 (
    .A(out1),
    .B(out2),
    .X(z5)
  );

  // z6 = AND(z4, z3)
  sky130_fd_sc_hd__and2_1 and4 (
    .A(z4),
    .B(z3),
    .X(z6)
  );

  // out3 = NOT(z5)
  sky130_fd_sc_hd__inv_1 inv5 (
    .A(z5),
    .Y(out3)
  );

  // z7 = OR(out3, z2)
  sky130_fd_sc_hd__or2_1 or3 (
    .A(out3),
    .B(z2),
    .X(z7)
  );

  // z8 = XOR(z6, z5)
  sky130_fd_sc_hd__xor2_1 xor2 (
    .A(z6),
    .B(z5),
    .X(z8)
  );

  // out4 = NOT(z7)
  sky130_fd_sc_hd__inv_1 inv6 (
    .A(z7),
    .Y(out4)
  );

  // z9 = AND(out4, z4)
  sky130_fd_sc_hd__and2_1 and5 (
    .A(out4),
    .B(z4),
    .X(z9)
  );

  // z10 = OR(z8, z7)
  sky130_fd_sc_hd__or2_1 or4 (
    .A(z8),
    .B(z7),
    .X(z10)
  );

  // out5 = XOR(z9, z10)
  sky130_fd_sc_hd__xor2_1 xor3 (
    .A(z9),
    .B(z10),
    .X(out5)
  );

  // Final output
  sky130_fd_sc_hd__inv_1 invOut (
    .A(out5),
    .Y(out)
  );
endmodule


/*
The Algorithm for this test is as follows

x = not(inp1)
y = not(inp2)
z0 = and(x, y)
z1 = or(x, y)
z2 = and(z0, z1)
out1 = not(z2)

z3 = and(out1, x)
z4 = or(z2, y)
out2 = not(z3)

z5 = xor(out1, out2)
z6 = and(z4, z3)
out3 = not(z5)

z7 = or(out3, z2)
z8 = xor(z6, z5)
out4 = not(z7)

z9 = and(out4, z4)
z10 = or(z8, z7)
out5 = xor(z9, z10)

out = not(out5)
*/