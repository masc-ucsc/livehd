// Formal phase-scheduler tracker: a LEAF WITH TWO CLOCK PORTS that the parent
// ties to one net.
//
// This is minion's prim_rf_*_preview shape (preview_clk_i + rf_clk_i, bound to
// the same net at all 32 instantiation sites) reduced to 14 lines. Proving that
// family is the largest single win available to the trust list, and it is the
// reason 2f-lec's clock analysis must be a DESIGN-WIDE, top-down pass over the
// clock inputs rather than something resolved per definition: the leaves-first
// driver proves every def with that def as its own top, and as a top these two
// ports are unbound free inputs. Today that is
//   `2 clock nets and no known integer ratio between them`
// even though the binding at the only instantiation site makes them one clock.
//
// The Pyrope twin is deliberately FLAT — same function, no child instance — so
// the pair also exercises the schedule surviving a hierarchy difference.
//
// Semantics: `rise` commits at the root rise; `fall` samples it at the FOLLOWING
// fall of the same period and therefore sees the NEW value.
module phase_two_clock_ports_cell (
  input  wire       ca,
  input  wire       cb,
  input  wire [2:0] d,
  output wire [2:0] q
);
  reg [2:0] rise;
  reg [2:0] fall;
  always @(posedge ca) rise <= d;
  always @(negedge cb) fall <= rise;
  assign q = fall;
endmodule

module phase_two_clock_ports (
  input  wire       clk,
  input  wire [2:0] d,
  output wire [2:0] q
);
  phase_two_clock_ports_cell u_cell(.ca(clk), .cb(clk), .d(d), .q(q));
endmodule
