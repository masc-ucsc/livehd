module test_and2(clk, in1, in2, out);
  input  clk; // Not used but for easier verilator common flow
  input  in1;
  input  in2;
  output out;

  wire  in1_buf, in1_not;
  wire  in2_buf, in2_not;
  wire  out_not;

  sky130_fd_sc_hd__inv_1 inv1 (
    .A(in1),
    .Y(in1_not)
  );

  sky130_fd_sc_hd__inv_1 inv2 (
    .A(in2),
    .Y(in2_not)
  );

  sky130_fd_sc_hd__buf_1 gin1 (
    .A(in1_not),
    .X(in1_buf)
  );

  sky130_fd_sc_hd__buf_1 gin2 (
    .A(in2_not),
    .X(in2_buf)
  );

  sky130_fd_sc_hd__and2_1 icg (
    .A(in1_buf),
    .B(in2_buf),
    .X(out_not)
  );

  sky130_fd_sc_hd__inv_1 invOut (
    .A(out_not),
    .Y(out)
  );
endmodule
