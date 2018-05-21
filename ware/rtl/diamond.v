module diamond
(
  input   Hi,
  input   Ci,
  
  output   Si
);

always @(*) begin
  Si = Hi ^ Ci;
end

endmodule

