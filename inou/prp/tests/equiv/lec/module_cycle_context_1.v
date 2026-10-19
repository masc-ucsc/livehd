// Constant binding only becomes local after the legalizer inlines the routers.
module router(input select_peer, local_data, peer_data, output y);
  assign y = select_peer ? peer_data : local_data;
endmodule

module module_cycle_context(input a, b, output x, y);
  router first (.select_peer(1'b0), .local_data(a), .peer_data(y), .y(x));
  router second(.select_peer(1'b1), .local_data(b), .peer_data(x), .y(y));
endmodule
