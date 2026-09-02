// pass.abc memory=true fixture (lhd_abc_memlower_test.sh): a packed register
// array written per entry (constant-index write ports: a synchronous reset plus
// a data write each), read by one port with a RUNTIME address AND read WHOLE
// (`assign all = mem`) — the reserved Memory `read_all` driver, the shape of
// br_tracker_linked_list_ctrl's `ll_head` array, which pass.abc used to refuse
// to bit-blast ("unmodeled memory output (read_all)").
module memall (
  input  logic            clk,
  input  logic            rst,
  input  logic [3:0]      we,
  input  logic [3:0][6:0] wdata,
  input  logic [1:0]      raddr,
  output logic [6:0]      rdata,
  output logic [27:0]     all
);
  logic [3:0][6:0] mem;
  for (genvar i = 0; i < 4; i++) begin : g
    always_ff @(posedge clk) begin
      if (rst) mem[i] <= '0;
      else if (we[i]) mem[i] <= wdata[i];
    end
  end
  assign rdata = mem[raddr];
  assign all   = mem;
endmodule
