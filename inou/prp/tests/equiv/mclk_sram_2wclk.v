// Multi-clock SRAM: two write ports on two DIFFERENT clocks (wclk_a, wclk_b)
// plus an async read. The LiveHD Memory model carries a single per-memory
// `clock_pin` (`clock_pin_refname` attribute is add-only), so the verilog->prp
// path (inou.slang -> upass.attributes) aborts when the second write port sets
// a different clock: "attribute clock_pin_refname of 'mem' is already set to a
// different value". cgen HAS a multi-clock memory wrapper
// (ware/rtl/cgen_memory_multiclock_1rd_1wr.v), but no front-end representation
// reaches it for per-port clocks. Tracked failing test (verilog->prp).
module mclk_sram_2wclk(
  input            wclk_a,
  input            wclk_b,
  input            wea,
  input            web,
  input      [3:0] addra,
  input      [3:0] addrb,
  input      [7:0] da,
  input      [7:0] db,
  input      [3:0] raddr,
  output     [7:0] rd
);
  reg [7:0] mem [0:15];
  always @(posedge wclk_a) if (wea) mem[addra] <= da;
  always @(posedge wclk_b) if (web) mem[addrb] <= db;
  assign rd = mem[raddr];
endmodule
