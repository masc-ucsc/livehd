// Golden for arm_local_merge: the same outputs written without temporaries.
module \arm_local_merge.arm (
   input  [1:0] s,
   input        c,
   input        d,
   input  [7:0] a,
   input  [7:0] b,
   output reg [8:0] x,
   output reg [7:0] y,
   output reg [7:0] z,
   output reg [7:0] w,
   output [7:0] q,
   input        clock,
   input        reset
);
  always @(*) begin
    if (c)
      x = (({1'b0, a} + {1'b0, b}) ^ 9'd3) | 9'h100;
    else if (d)
      x = {1'b0, (a & b) | 8'd1};
    else
      x = 9'd0;
  end

  always @(*) begin
    if (c)
      y = (d ? (a ^ b) : b) ^ 8'h5a;
    else
      y = a & 8'h0f;
  end

  always @(*) begin
    case (s)
      2'd0:    z = (a & 8'h0f) | 8'h10;
      2'd1:    z = b ^ a;
      2'd2:    z = b & 8'hf0;
      default: z = a;
    endcase
  end

  always @(*) begin
    case (s)
      2'd0:    w = a | b;
      2'd1:    w = (a & b) ^ 8'hff;
      default: w = b;
    endcase
  end

  reg [7:0] r;
  always @(posedge clock) begin
    if (reset)
      r <= 8'd0;
    else if (c)
      r <= a ^ b;
  end
  assign q = r;
endmodule
