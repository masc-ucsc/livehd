// :test: roundtrip
// :top: flat
package flat_pkg;
  typedef struct packed {
    logic        bid;
    logic        read_en;
    logic        size64;
    logic [15:0] addr;
    logic [7:0]  other;
  } ctrl_t;
endpackage
module flat (
  input  logic enabled, input logic enabled_int, input logic req_int8,
  input  logic doing_a, input logic a_skip, input logic [15:0] addr_i,
  output flat_pkg::ctrl_t ctrl,
  output logic req_a_o
);
  logic req_a;
  always_comb begin
    ctrl.other   = 8'd0;
    ctrl.read_en = enabled_int && (doing_a && ~a_skip);
    ctrl.size64  = req_int8;
    ctrl.addr    = addr_i;
    ctrl.bid     = ~enabled ? 1'b0 : req_int8 ? ctrl.read_en : req_a;
  end
  always_comb begin
    req_a = (ctrl.read_en || (enabled_int && a_skip)) && doing_a;
  end
  assign req_a_o = req_a;
endmodule
