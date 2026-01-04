module diamond
(
  input   Hi,
  input   Ci,

  output  Si
);

assign Si = Hi ^ Ci;

endmodule
