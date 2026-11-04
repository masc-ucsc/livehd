// Variables written by more than one clocked process (IEEE 1800 forbids this for always_ff). lhd_ff_merge
// keeps every write, and the process later in the source wins in a cycle where several write (the native
// slang reader's rule); both yosys readers used to drop one of the writers.
module ff_merge(input logic clk, input logic a, input logic b, input logic c, input logic [3:0] d,
                output logic [3:0] q, output logic [3:0] r, output logic [3:0] s);
  // Both conditional: a cycle with only `a` must keep 5, one with `b` gives 9.
  logic [3:0] x;
  always_ff @(posedge clk) if (a) x <= 4'd5;
  always_ff @(posedge clk) if (b) x <= 4'd9;

  // Partial overlap: the second process writes only the low half.
  logic [3:0] y;
  always_ff @(posedge clk) if (a) y <= d;
  always_ff @(posedge clk) if (b) y[1:0] <= 2'd3;

  // Three writers; the middle one reads the variable (its CURRENT value, not the first process's write).
  // (Simulators differ in a cycle where several write: here Icarus keeps the first process's write and
  // Verilator the middle one's. The test checks against the native reader.)
  logic [3:0] z;
  always_ff @(posedge clk) if (a) z <= 4'd1;
  always_ff @(posedge clk) if (b) z <= z + 4'd2;
  always_ff @(posedge clk) if (c) z <= 4'd7;

  assign q = x;
  assign r = y;
  assign s = z;
endmodule
