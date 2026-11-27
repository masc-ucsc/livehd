module tuple_field_instance_collision(input [7:0] a, output [7:0] y, output [7:0] z, output [7:0] q, output [7:0] named);
  assign y = a;
  assign z = a ^ 8'd1;
  assign q = a;
  assign named = a ^ 8'd1;
endmodule
