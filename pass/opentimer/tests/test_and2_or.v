module test_and2(clk, in1, in2, out);
  input  clk; // Not used but for easier verilator common flow
  input  in1;
  input  in2;
  output out;

  wire  x, y;
  wire  z0, z1;
  wire  z;
  wire  in1_buf;
  wire  in2_buf;

  sky130_fd_sc_hd__inv_1 inv1 (
    .A(in1),
    .Y(in1_buf)
  );

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

  sky130_fd_sc_hd__and2_1 and_gate (
    .A(x),
    .B(y),
    .X(z0)
  );

  sky130_fd_sc_hd__or2_1 or_gate (
    .A(x),
    .B(y),
    .X(z1)
  );

  sky130_fd_sc_hd__and2_1 and_gate2 (
    .A(z0),
    .B(z1),
    .X(z)
  );

  sky130_fd_sc_hd__inv_1 invOut (
    .A(z),
    .Y(out)
  );

endmodule


/*
The Algorithm for this test is as follows

x = not(inp1)
y = not(inp2)
z0 = and(x,y)
z1 = or(x,y)
z = and(z0,z1)
out = not(z)
*/