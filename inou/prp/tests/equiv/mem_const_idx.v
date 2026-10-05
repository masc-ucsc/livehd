module \mem_const_idx.cidx (
  input            clock,
  input      [7:0] a,
  input      [7:0] b,
  input            we,
  output     [7:0] z
);

  reg [7:0] t[1:0];

  always @(posedge clock) begin
    if (we) begin
      t[0] <= a;
      t[1] <= b;
    end
  end

  assign z = we ? b : t[1];

endmodule
