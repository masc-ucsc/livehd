// two unconditional writes to the SAME address: last wins (wd2)
module mem_wr_seq_same(input logic clk,
                       input logic [1:0] waddr, input logic [1:0] wd1,
                       input logic [1:0] wd2,
                       input logic [1:0] raddr, output logic [1:0] rdata);
  logic [1:0] mem [4];
  always_ff @(posedge clk) begin
    mem[waddr] <= wd1;
    mem[waddr] <= wd2;
  end
  assign rdata = mem[raddr];
endmodule
