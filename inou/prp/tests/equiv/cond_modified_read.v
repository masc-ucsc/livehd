module \cond_modified_read.condread (
  input s,
  input b,
  output g
);

  assign g = (s ? 1'b1 : 1'b0) & b;

endmodule
