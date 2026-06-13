module \mem_init.mem_init (
  input            clock,
  input            reset,
  input      [1:0] raddr,
  output     [7:0] q
);

  reg [7:0] mem [0:3];

  initial begin
    mem[0] = 8'd1; mem[1] = 8'd2; mem[2] = 8'd3; mem[3] = 8'd4;
  end

  always @(posedge clock) begin
    if (reset) begin
      mem[0] <= 8'd1; mem[1] <= 8'd2; mem[2] <= 8'd3; mem[3] <= 8'd4;
    end
  end

  // async read; restore ports are never forwarded (a read during reset
  // returns the committed contents)
  assign q = mem[raddr];

endmodule
