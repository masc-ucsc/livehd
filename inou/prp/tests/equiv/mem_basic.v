module \mem_basic.memdut (
  input            clock,
  input      [7:0] a,
  input      [1:0] wsel,
  input      [1:0] rsel,
  input            we,
  output     [7:0] z
);

  reg [7:0] data[3:0];

  always @(posedge clock) begin
    if (we) data[wsel] <= a;
  end

  // async read of the current address + same-cycle write forwarding
  assign z = (we && (wsel == rsel)) ? a : data[rsel];

endmodule
