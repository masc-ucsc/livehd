module packed_leaf(input logic fb, input logic a, output logic o);
  always_comb o = fb | a;
endmodule

module packed_sub_boundary(input logic a, output logic z);
  logic child_o;
  logic [15:0] lane;
  logic [15:0] bus;
  logic fb;
  always_comb begin
    lane = ({15'b0, child_o} << 8) & 16'hffff;
    bus = 16'b0 | lane;
    fb = bus[0];
    z = child_o;
  end
  packed_leaf u_leaf(.fb(fb), .a(a), .o(child_o));
endmodule
