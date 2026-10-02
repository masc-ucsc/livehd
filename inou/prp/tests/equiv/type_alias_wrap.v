// Golden for type_alias_wrap.prp: a 5-bit and a 6-bit wrapping counter.
module type_alias_wrap(
    input        clk,
    input        rst,
    input        en,
    output [4:0] o,
    output [5:0] c
);
  reg [4:0] r;
  reg [5:0] n;
  assign o = r;
  assign c = n;
  always @(posedge clk) begin
    if (rst) begin
      r <= 5'd0;
      n <= 6'd0;
    end else if (en) begin
      r <= r + 5'd1;
      n <= n + 6'd3;
    end
  end
endmodule
