
module test_and2(clk, in1, in2, out);
  input  clk; // Not used but for easier verilator common flow
  input  in1;
  input  in2;
  output out;

  wire  in1_buf;
  wire  in2_buf;

  sky130_fd_sc_hd__and2_1 icg (
    .A(in1_buf),
    .B(in2_buf),
    .X(out)
  );
  sky130_fd_sc_hd__buf_1 gin1 (
    .A(in1),
    .X(in1_buf)
  );
  sky130_fd_sc_hd__buf_1 gin2 (
    .A(in2),
    .X(in2_buf)
  );
endmodule
