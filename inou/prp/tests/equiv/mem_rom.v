module \mem_rom.romtest (
  input      [1:0] rsel,
  output     [7:0] z
);

  assign z = (rsel == 2'd0) ? 8'd10
           : (rsel == 2'd1) ? 8'd20
           : (rsel == 2'd2) ? 8'd30
           :                  8'd40;

endmodule
