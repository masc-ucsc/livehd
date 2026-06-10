module \mem_mut_array.mutarr (
  input      [7:0] a,
  input      [1:0] wsel,
  input      [1:0] rsel,
  output     [7:0] z
);

  // per-cycle default = init contents; the write overrides its entry
  wire [7:0] init_val = (rsel == 2'd0) ? 8'd1
                      : (rsel == 2'd1) ? 8'd2
                      : (rsel == 2'd2) ? 8'd3
                      :                  8'd4;

  assign z = (wsel == rsel) ? a : init_val;

endmodule
