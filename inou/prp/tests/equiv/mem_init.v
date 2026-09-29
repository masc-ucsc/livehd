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

  // The initializer is the reset value of every entry, restored in ONE cycle
  // of reset exactly like a scalar reg (the memory's whole-array reset).
  always @(posedge clock) begin
    if (reset) begin
      mem[0] <= 8'd1; mem[1] <= 8'd2; mem[2] <= 8'd3; mem[3] <= 8'd4;
    end
  end

  // async read
  assign q = mem[raddr];

endmodule
