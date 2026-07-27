// sync write, sync read (data register): read-first on same-addr collision
module mem_sw_sr_data(input logic clk, input logic we,
                      input logic [1:0] waddr, input logic [1:0] wdata,
                      input logic [1:0] raddr, output logic [1:0] rdata);
  logic [1:0] mem [4];
  always_ff @(posedge clk) begin
    if (we) mem[waddr] <= wdata;
    rdata <= mem[raddr];
  end
endmodule
