// :test: roundtrip
// :top: whole
package whole_pkg;
  typedef struct packed { logic x; logic [3:0] y; } a_t;
  typedef struct packed { logic p; logic [3:0] q; } b_t;
  typedef struct packed { a_t a; b_t b; } ctrl_t;
endpackage
module whole (
  input  logic enabled, input logic doing_a, input logic pi, input logic [3:0] qi,
  input  logic [3:0] yi,
  output whole_pkg::ctrl_t ctrl,
  output logic req_o
);
  logic req_a;
  always_comb begin
    ctrl.b.p = pi;
    ctrl.b.q = qi;
    ctrl.a.y = yi;
    // writes a leaf of sub-struct `a` ...
    ctrl.a.x = enabled ? req_a : 1'b0;
  end
  always_comb begin
    // ... fed by a WHOLE read of the DISJOINT sub-struct `b`. Acyclic by field;
    // a fallback that reads the whole PORT drags in ctrl.a.x and cycles.
    req_a = (ctrl.b != 5'd0) && doing_a;
  end
  assign req_o = req_a;
endmodule
