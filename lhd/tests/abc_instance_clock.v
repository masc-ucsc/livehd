// A register clocked straight by the output of a module instance that sits in
// the SAME module as the register: NVDLA's per-block `slcg` wrapper around a
// flop-based clock gate (negedge-sampled enable ANDed with the clock) whose
// clock feeds a flattened sibling block's registers. The instance is a native
// boundary of the register's region, so its output pin is neither a region
// input nor a recognized latch+AND clock gate.
module abc_ic_gate (
    input  clk,
    input  en,
    output gclk
);
  reg en_n;
  always @(negedge clk) en_n <= en;
  assign gclk = clk & en_n;
endmodule

module abc_instance_clock (
    input        clk,
    input        en_a,
    input        en_b,
    input  [3:0] d,
    output [3:0] qa,
    output [3:0] qb
);
  wire gclk_a, gclk_b;
  // Two instances of the gate, so the gate stays a module of its own.
  abc_ic_gate ga (.clk(clk), .en(en_a), .gclk(gclk_a));
  abc_ic_gate gb (.clk(clk), .en(en_b), .gclk(gclk_b));
  reg [3:0] d1;
  always @(posedge clk) d1 <= d;
  reg [3:0] ra;
  always @(posedge gclk_a) ra <= d1 + 4'd1;
  reg [3:0] rb;
  always @(posedge gclk_b) rb <= d1 ^ 4'd5;
  assign qa = ra;
  assign qb = rb;
endmodule
