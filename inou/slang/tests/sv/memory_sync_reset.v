// A SYNCHRONOUS reset arm on a native memory must clear written state again on
// every reset, not only initialize it at power-on. The reader used to peel the
// arm off a packed memory into `initial=0, reset_pin=rst` attrs, which memory
// lowering treats as power-on INIT only: after a write, a later `rst` pulse no
// longer cleared the entry. Native `lhd lec` is blind to that (its after_reset
// phase holds the reset-named input deasserted after the prologue), so this
// fixture opts into the lgyosys cross-check, whose lgcheck miter re-asserts
// `rst` after writes and must PROVE the emission (exit 0). The packed spelling
// is the one the old peel hit; the unpacked one, in its own process, is the
// control that was never peeled.
// :lec_solver: lgyosys
module memory_sync_reset(input clk, rst, we, input [4:0] d,
                         output [4:0] q_packed, q_unpacked);
  reg [0:0][4:0] packed_mem;
  reg [4:0] unpacked_mem [0:0];
  always @(posedge clk) if (rst) packed_mem[0] <= 0; else if (we) packed_mem[0] <= d;
  always @(posedge clk) if (rst) unpacked_mem[0] <= 0; else if (we) unpacked_mem[0] <= d;
  assign q_packed = packed_mem[0];
  assign q_unpacked = unpacked_mem[0];
endmodule
