// Regression fixture for submodule output-port inlining in the v->prp writer.
// `top` instantiates a multi-output stateful submodule and reads scalar and
// nested bundle outputs AFTER the instance (backward-only). The writer must
// drop the per-output extraction temps + their wires and read
// `u_sub.<port-path>` directly.
typedef struct packed {
  logic [7:0] id;
  logic [7:0] data;
} req_t;

module submod (
  input            clock,
  input      [7:0] a,
  input      [7:0] b,
  output reg [7:0] s,
  output reg [7:0] d,
  output req_t     l2_req_data_o
);
  always @(posedge clock) begin
    s <= a + b;
    d <= a - b;
    l2_req_data_o.id   <= a;
    l2_req_data_o.data <= b;
  end
endmodule

module top (
  input            clock,
  input      [7:0] x,
  input      [7:0] y,
  output     [7:0] o
);
  wire [7:0] ss, dd;
  req_t req;
  submod u_sub (.clock(clock), .a(x), .b(y), .s(ss), .d(dd), .l2_req_data_o(req));
  assign o = ss ^ dd ^ req.id;   // scalar and nested outputs used after the instance -> inline
endmodule
