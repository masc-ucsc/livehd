// comb_latch.sv with its two latches written as `always_latch`: the reference the yosys-slang netlist of
// comb_latch.sv must be equivalent to.
module comb_latch(input logic we, input logic wr, input logic [1:0] rdy, input logic c, input logic [3:0] d,
                  output logic q, output logic [2:0] busy_o, output logic [3:0] r, output logic [3:0] s);
  logic b, busy2;
  always_latch if (!we) b = !wr;
  always_latch if (!we) busy2 = !wr;
  assign q = b;
  assign busy_o = {busy2, !rdy[1], !rdy[0]};
  assign r = c ? d : 4'd0;
  assign s = c ? d + 4'd1 : 4'd0;
endmodule
