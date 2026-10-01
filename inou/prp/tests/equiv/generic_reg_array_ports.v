// Golden for generic_reg_array_ports: two packed shift registers (`a`, `b`);
// the outputs read the NEXT values (n*/m*) because the Pyrope reads follow the
// shift in program order.
module generic_reg_array_ports (
  input         clock,
  input         reset,
  input         en,
  input  [7:0]  d,
  input  [31:0] v4,
  input  [14:0] v3,
  output [7:0]  q8,
  output [4:0]  q5,
  output [31:0] o8,
  output [14:0] o5
);
  reg  [31:0] a;
  reg  [14:0] b;
  wire [7:0] a0 = a[7:0], a1 = a[15:8], a2 = a[23:16], a3 = a[31:24];
  wire [4:0] b0 = b[4:0], b1 = b[9:5], b2 = b[14:10];
  wire       w  = en && !reset;
  wire [7:0] n0 = w ? d : a0, n1 = w ? a0 : a1, n2 = w ? a1 : a2, n3 = w ? a2 : a3;
  wire [4:0] m0 = w ? d[4:0] : b0, m1 = w ? b0 : b1, m2 = w ? b1 : b2;
  always @(posedge clock) begin
    if (reset) begin
      a <= 32'd0;
      b <= 15'd0;
    end else begin
      a <= {n3, n2, n1, n0};
      b <= {m2, m1, m0};
    end
  end
  assign q8 = n3;
  assign q5 = m2;
  assign o8 = v4 ^ {n3, n2, n1, n0};
  assign o5 = v3 ^ {m2, m1, m0};
endmodule
