module port_named_like_module(input [7:0] d, output port_named_like_module);
  assign port_named_like_module = ^d;
endmodule
