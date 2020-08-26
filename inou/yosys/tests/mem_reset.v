
module mem_reset
( input                  clk,
  input                  reset,

  input [6-1:0]    addr,

  input [7:0]      a,
  input [7:0]      b,
  output [7:0]     out
);


  always @(posedge clk) begin
    if (reset) begin
      to2_a <= 'bx;
      to2_aValid <= 0;
    end else begin
      to2_a    <= to1_a + to1_b + 2;
      to2_aValid <= to1_aValid;
    end
  end

  logic [6-1:0] wr_addr;
  logic [6-1:0] wr_addr_next;

  always_comb begin
    if (a < b)
      wr_addr_next = a;
    else
      wr_addr_next = b;
  end

  always @(posedge clk) begin
    if (reset) begin
      wr_addr <= 33+b;
    end else begin
      wr_addr <= wr_addr_next;
    end
  end

  logic [6-1:0] reset_cntr;

  logic [7:0] mem[0:31];

  always @(posedge clk) begin
    if (reset) begin
      mem[reset_cntr] <= reset_cntr;
      reset_cntr <= reset_cntr + 1;
    end else begin
      mem[wr_addr] <= a+b;
    end
  end

  assign out = mem[b];

endmodule

