// two guarded writes in one block: second write wins on same-addr collision
module mem_two_wr_prio(input logic clk, input logic we0, input logic we1,
                       input logic [1:0] wa0, input logic [1:0] wd0,
                       input logic [1:0] wa1, input logic [1:0] wd1,
                       input logic [1:0] raddr, output logic [1:0] rdata);
  logic [1:0] mem [4];
  always_ff @(posedge clk) begin
    if (we0) mem[wa0] <= wd0;
    if (we1) mem[wa1] <= wd1;
  end
  assign rdata = mem[raddr];
endmodule
