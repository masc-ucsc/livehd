// The generic mesh has a candidate internal cycle; its caller makes it acyclic.
module router(input select_peer, local_data, peer_data, output y);
  assign y = select_peer ? peer_data : local_data;
endmodule

module mesh(input select_a, select_b, a, b, output x, y);
  router first (.select_peer(select_a), .local_data(a), .peer_data(y), .y(x));
  router second(.select_peer(select_b), .local_data(b), .peer_data(x), .y(y));
endmodule

module module_cycle_context(input a, b, output x, y);
  mesh pair(.select_a(1'b0), .select_b(1'b1), .a(a), .b(b), .x(x), .y(y));
endmodule
