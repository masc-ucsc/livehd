// Combinational blocks that leave a variable unassigned on some path. IEEE 1800: the variable keeps its
// value (a latch; tools should warn), and simulators hold it. lhd_comb_latch makes the yosys-slang reader
// build those latches; before, each was a combinational loop that pass.legalize refused.
module comb_latch(input logic we, input logic wr, input logic [1:0] rdy, input logic c, input logic [3:0] d,
                  output logic q, output logic [2:0] busy_o, output logic [3:0] r, output logic [3:0] s);
  // 1. a one-bit latch: b holds while we = 1
  logic b;
  always_comb if (!we) b = !wr;
  assign q = b;

  // 2. one element of an array left unassigned (SoomRV SoC.sv:182): only busy[2] is a latch
  logic busy [2:0];
  always_comb begin
    for (integer i = 0; i < 2; i = i + 1) busy[i] = !rdy[i];
    if (!we) busy[2] = !wr;
  end
  assign busy_o = {busy[2], busy[1], busy[0]};

  // 3. assigned on every path: plain logic, no latch
  logic [3:0] t;
  always_comb begin
    t = 4'd0;
    if (c) t = d;
  end
  assign r = t;

  // 4. written only in the branch that reads it: the hold is never read, no latch
  logic [3:0] u;
  always_comb begin
    s = 4'd0;
    if (c) begin
      u = d + 4'd1;
      s = u;
    end
  end
endmodule
