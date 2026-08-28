module scalar_initial_no_reset (
    input  logic        clk,
    input  logic        rst,
    input  logic        d,
    output logic [23:0] out
);
  logic [7:0] decl_q = 8'hA5;
  logic [7:0] block_q;
  logic [7:0] reset_q = 8'h55;

  initial block_q = 8'h3C;

  always_ff @(posedge clk) begin
    decl_q  <= {decl_q[6:0], d};
    block_q <= {block_q[6:0], d};
  end

  always_ff @(posedge clk or posedge rst) begin
    if (rst)
      reset_q <= 8'h11;
    else
      reset_q <= {reset_q[6:0], d};
  end

  assign out = {decl_q, block_q, reset_q};
endmodule
