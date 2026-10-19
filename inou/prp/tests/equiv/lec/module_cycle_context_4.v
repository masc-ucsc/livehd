// The routing constants can be module outputs, as in the Eyeriss controller.
module configuration(output select_a, select_b);
  assign select_a = 1'b0;
  assign select_b = 1'b1;
endmodule

module router(input select_peer, local_data, peer_data, output y);
  assign y = select_peer ? peer_data : local_data;
endmodule

module mesh(input select_a, select_b, a, b, output x, y);
  router first (.select_peer(select_a), .local_data(a), .peer_data(y), .y(x));
  router second(.select_peer(select_b), .local_data(b), .peer_data(x), .y(y));
endmodule

module module_cycle_context(input a, b, output x, y);
  wire select_a, select_b;
  configuration config_instance(.select_a(select_a), .select_b(select_b));
  mesh pair(.select_a(select_a), .select_b(select_b), .a(a), .b(b), .x(x), .y(y));
endmodule
