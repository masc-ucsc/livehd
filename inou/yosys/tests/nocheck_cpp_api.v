

module cpp_api(input [7:0] a, output [7:0] d);

  wire [7:0] b;

  lgcpp_test gen_b(.out(b));

  assign d = b + a;

endmodule

