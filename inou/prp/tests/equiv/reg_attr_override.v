module \reg_attr_override.blink (
  input        clock,
  input        rst_n,
  input        en,
  output [7:0] q
);

  reg [7:0] r;
  assign q = r;

  always @(posedge clock or negedge rst_n) begin
    if (!rst_n) r <= 8'd5;
    else if (en && r < 8'd200) r <= r + 8'd1;
  end

endmodule
