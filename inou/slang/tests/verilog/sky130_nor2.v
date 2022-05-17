
module sky130_nor2(out, inp1, inp2);
  input inp1;
  input inp2;
  output out;
  sky130_fd_sc_hs__nor2_1 nor2 (
    .A(inp1),
    .B(inp2),
    .Y(out)
  );
endmodule
