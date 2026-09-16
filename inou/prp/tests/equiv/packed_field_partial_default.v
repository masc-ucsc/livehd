module packed_field_partial_default(
  input logic sel,
  input logic [39:0] x,
  input logic [1:0] a, b,
  output struct packed { logic [255:0] zero, seeded; } out
);
  always_comb begin
    out.zero = '0;
    out.seeded = (256'd1 << 200) | 256'h123456789abcdef0;
    if (sel) begin
      out.zero[39:0] = x;
      out.seeded[39:0] = x;
    end else begin
      out.zero[6:0] = {a, b, 3'b000};
      out.seeded[6:0] = {a, b, 3'b000};
    end
  end
endmodule
