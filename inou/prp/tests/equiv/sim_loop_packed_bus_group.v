module sim_loop_packed_bus_group(
  input  [3:0]  lo,
  input         ren,
  input  [1:0]  addr,
  input  [31:0] x,
  output [7:0]  z
);
  // bus[13:12] carries only `addr`; bus[11:4] carries only rdata. The packed
  // bus is a pure concat, so the read collapses to a plain dynamic select.
  assign z = x >> (addr * 8);
endmodule
