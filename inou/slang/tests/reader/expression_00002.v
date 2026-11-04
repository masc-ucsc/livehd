module expression_00002(a0, a1, b0, b1, y);
  input [3:0] a0;
  input [5:0] a1;

  input [3:0] b0;
  input [5:0] b1;

  wire [3:0] y0;
  wire [5:0] y1;
  wire [3:0] y2;
  wire [5:0] y3;
  wire [3:0] y4;
  wire [5:0] y5;

  output [29:0] y;
  assign y = {y0,y1,y2,y3,y4,y0};

  localparam [3:0] p0 = (2'd1);
  localparam [5:0] p1 = {(3'd0),(3'd7)};
  localparam [3:0] p2 = (5'd10);
  localparam [5:0] p3 = {((4'd15)?(2'sd0):(4'sd3)),(((4'd15)^(5'd31))|((5'd27)?(3'd5):(2'd1))),((4'd5)?(4'd7):(5'd21))};
  localparam [3:0] p4 = (^((-5'sd0)<=(2'd2)));
  localparam [5:0] p5 = ((((5'sd15)|(4'd11))==((3'd0)?(3'd7):(-5'sd4)))&({4{(3'd2)}}=={1{((3'd5)?(4'd11):(4'd13))}}));

  assign y0 = (((~a1)!=(a1<a0))!=((b0>>>a1)<=(^a1)))?1:0;
  assign y1 = (!(~{{3{(4'sd7)}},((p2<p3)=={p0,p2})}));
  assign y2 = {p3,b0};
  assign y3 = ({1{(a0<<p3)}}<=(p3^~a0));
  assign y4 = (~|((p0-b0)<={1{b0}}));
  assign y5 = (((|a0)?(6'd2 ** p2):$unsigned(b1))>>>((a1/b0)|(~^(b1<=b0))));
endmodule
